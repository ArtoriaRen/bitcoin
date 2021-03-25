/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <locale>
#include<string.h>
#include <netinet/in.h>
#include "pbft/pbft.h"
#include "validation.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "consensus/tx_verify.h"
#include "netmessagemaker.h"
#include <memory>
#include "netmessagemaker.h"
#include "init.h"
#include "streams.h"
#include "clientversion.h"

extern std::unique_ptr<CConnman> g_connman;

int32_t pbftID; 
int32_t QSizePrintPeriod;
int32_t maxBlockSize = 2000;
int32_t nWarmUpBlocks;
bool testStarted = false;
int32_t reqWaitTimeout = 1000;

CPbft::CPbft(): localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastExecutedSeq(-1), client(nullptr), peers(std::vector<CNode*>(groupSize)), nReqInFly(0), nCompletedTx(0), clientConnMan(nullptr), lastQSizePrintTime(std::chrono::milliseconds::zero()), totalExeTime(0), lastReplySentSeq(-1), notEnoughReqStartTime(std::chrono::milliseconds::zero()), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    pubKeyMap.insert(std::make_pair(pbftID, myPubKey));
}

ThreadSafeQueue::ThreadSafeQueue() { }

ThreadSafeQueue::~ThreadSafeQueue() { }

CTransactionRef& ThreadSafeQueue::front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    return queue_.front();
}

std::deque<CTransactionRef> ThreadSafeQueue::get_all() {
    std::unique_lock<std::mutex> mlock(mutex_);
    std::deque<CTransactionRef> ret(queue_);
    queue_.clear();
    return ret;
}

std::deque<CTransactionRef> ThreadSafeQueue::get_upto(uint32_t upto) {
    std::unique_lock<std::mutex> mlock(mutex_);
    if (queue_.size() < upto) {
	std::deque<CTransactionRef> ret(queue_);
	queue_.clear();
	return ret;
    } else {
	std::deque<CTransactionRef> ret;
	ret.insert(ret.end(), queue_.begin(), queue_.begin() + upto);
	queue_.erase(queue_.begin(), queue_.begin() + upto);
	return ret;
    }
}

void ThreadSafeQueue::pop_front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    queue_.pop_front();
}

void ThreadSafeQueue::push_back(const CTransactionRef& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

void ThreadSafeQueue::push_back(CTransactionRef&& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(std::move(item));
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

int ThreadSafeQueue::size() {
    std::unique_lock<std::mutex> mlock(mutex_);
    int size = queue_.size();
    mlock.unlock();
    return size;
}

bool ThreadSafeQueue::empty() {
    std::unique_lock<std::mutex> mlock(mutex_);
    return queue_.empty();
}

bool CPbft::ProcessPP(CConnman* connman, CPre_prepare& ppMsg) {
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if (!checkMsg(&ppMsg)) {
        return false;
    }

    // check if the digest matches client req
    ppMsg.pPbftBlock->ComputeHash();
    if (ppMsg.digest != ppMsg.pPbftBlock->hash) {
	std::cerr << "digest does not match block hash tx. block hash = " << ppMsg.pPbftBlock->hash.GetHex() << ", but digest = " << ppMsg.digest.GetHex() << std::endl;
	return false;
    }
    // add to log
    log[ppMsg.seq].ppMsg = ppMsg;
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */

    //std::cout << "digest = " << ppMsg.digest.GetHex() << std::endl;

    /* Enter prepare phase. */
    log[ppMsg.seq].phase = PbftPhase::prepare;
    /* make a pMsg */
    CPbftMessage pMsg = assembleMsg(ppMsg.seq);
    /* send the pMsg to other peers, including the leader. */
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    int32_t start_peerID = pbftID - pbftID % groupSize;
    int32_t end_peerID = start_peerID + groupSize;
    std::cout << "sending PREPARE msg" << std::endl;
    for (int32_t i = start_peerID; i < end_peerID; i++) {
	/* do not send a msg to myself. */
	if (i == pbftID)
	    continue; 
	connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::PBFT_P, pMsg));
    }
    /* add the pMsg to our own log.
     * We should do this after we send the pMsg to other peers so that we always 
     * send pMsg before cMsg. 
     */
    ProcessP(connman, pMsg, false);
    return true;
}

bool CPbft::ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(&pMsg)) {
	    return false;
	}

	// check the message's view mactches the ppMsg's view in log.
	if (log[pMsg.seq].ppMsg.view != pMsg.view) {
	    std::cerr << "log entry view = " << log[pMsg.seq].ppMsg.view 
		    << ", but msg view = " << pMsg.view << std::endl;
	    return false;
	}
    }

    /* add to log (currently use placeholder: should add the entire message 
     * to log and not increase re-count if the sender is the same. 
     * Also, if prepares are received earlier than pre-prepare, different
     * prepare may have different digest. Should categorize buffered prepares 
     * based on digest.)
     */
    // log[prepare.seq].prepareArray.push_back(prepare);

    /* count the number of prepare msg. enter commit if greater than 2f */
    log[pMsg.seq].prepareCount++;
    /* In the if condition, we use == (nFaulty << 1) instead of >= (nFaulty << 1),
     * so that we do not re-send commit msg every time another prepare msg is received.
     */
    if (log[pMsg.seq].phase == PbftPhase::prepare && log[pMsg.seq].prepareCount >= (nFaulty << 1)) {
	/* Enter commit phase. */
        log[pMsg.seq].phase = PbftPhase::commit;
	/* make a cMsg */
	CPbftMessage cMsg = assembleMsg(pMsg.seq);
	/* send the cMsg to other peers */
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
        int32_t start_peerID = pbftID - pbftID % groupSize;
	int32_t end_peerID = start_peerID + groupSize;
    std::cout << "sending COMMIT msg" << std::endl;
	for (int32_t i = start_peerID; i < end_peerID; i++) {
	    /* do not send a msg to myself. */
	    if (i == pbftID)
		continue; 
	    connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::PBFT_C, cMsg));
	}
	/* add the cMsg to our own log.
	 * We should do this after we send the cMsg to other peers so that we always 
	 * send cMsg before execute tx. 
	 */
	ProcessC(connman, cMsg, false);
    }
    return true;
}

bool CPbft::ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(&cMsg)) {
	    return false;
	}

	// check the message's view mactches the ppMsg's view in log.
	if (log[cMsg.seq].ppMsg.view != cMsg.view) {
	    std::cerr << "log entry view = " << log[cMsg.seq].ppMsg.view 
		    << ", but msg view = " << cMsg.view << std::endl;
	    return false;
	}
    }

    // count the number of commit msg. enter execute if greater than 2f+1
    log[cMsg.seq].commitCount++;
    if (log[cMsg.seq].phase == PbftPhase::commit && log[cMsg.seq].commitCount >= (nFaulty << 1) + 1) {
        // enter reply phase
        std::cout << "block " << cMsg.seq << " enters reply phase" << std::endl;
        log[cMsg.seq].phase = PbftPhase::reply;

        if (cMsg.seq == 0) {
            /* log that test has started. */
            struct timeval curTime;
            gettimeofday(&curTime, NULL);
            thruputLogger.logServerSideThruput(curTime, 0);
        }

        /* the log-exe thread will execute blocks in reply phase sequentially. */
    }
    return true;
}

bool CPbft::checkMsg(CPbftMessage* msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(msg->peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << msg->peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    msg->getHash(msgHash);
    if (!it->second.Verify(msgHash, msg->vchSig)) {
        std::cerr << "verification sig fail" << std::endl;
        return false;
    }
    // server should be in the view
    if (localView != msg->view) {
        std::cerr << "server view = " << localView << ", but msg view = " << msg->view << std::endl;
        return false;
    }

    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if (!log[msg->seq].ppMsg.digest.IsNull() && log[msg->seq].ppMsg.digest != msg->digest) {
        std::cerr << "digest error for log entry " << msg->seq << ". digest in log = " << log[msg->seq].ppMsg.digest.GetHex() << ", but msg->digest = " << msg->digest.GetHex() << std::endl;
        return false;
    }


    return true;
}

CPre_prepare CPbft::assemblePPMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn) {
    CPre_prepare toSent; // phase is set to Pre_prepare by default.
    toSent.seq = nextSeq++;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.pPbftBlock = pPbftBlockIn;
    toSent.digest = toSent.pPbftBlock->hash;
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so client tx is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CPbftMessage CPbft::assembleMsg(uint32_t seq) {
    CPbftMessage toSent(log[seq].ppMsg);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CReply CPbft::assembleReply(std::deque<uint256>& vTx, const char exe_res) const {
    /* 'y' --- execute sucessfully
     * 'n' --- execute fail
     */
    CReply toSent(exe_res, std::move(vTx));
    //uint256 hash;
    //toSent.getHash(hash);
    //privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

bool CPbft::executeLog(struct timeval& start_process_first_block) {
    struct timeval start_time, end_time;
    /* execute all lower-seq tx until this one if possible. */
    int lastExecutedSeqStart = lastExecutedSeq;
    uint i = lastExecutedSeq + 1;
    /* We should go on to execute all log slots that are in reply phase even
     * their seqs are greater than the seq passed in. If we only execute up to
     * the seq passed in, a slot missing a pbftc msg might permanently block
     * log slots after it to be executed. */
    for (; i < logSize && log[i].phase == PbftPhase::reply; i++) {
        gettimeofday(&start_time, NULL);
        log[i].txCnt = log[i].ppMsg.pPbftBlock->Execute(i, *pcoinsTip);
        gettimeofday(&end_time, NULL);
        lastExecutedSeq = i;
        nCompletedTx += log[i].txCnt;
        thruputLogger.logServerSideThruput(end_time, nCompletedTx);
        std::cout << "Average combined verify and execution time of block " << i << ": " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / log[i].txCnt << " us/req. Total executed tx cnt = " << nCompletedTx << std::endl;
    }
    /* We have done something useful if the lastExecutedSeq is moved forward. */
    return lastExecutedSeq != lastExecutedSeqStart;
}

bool CPbft::sendReplies(CConnman* connman) {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    std::deque<uint256> completedTx;
    if (lastExecutedSeq > lastReplySentSeq) {
        /* sent reply msg for only one block per loop .*/
        int seq = ++lastReplySentSeq;
        std::cout << "sending reply for block " << seq <<",  lastReplySentSeq = "<<  lastReplySentSeq << std::endl;
        std::vector<CTransactionRef>& txList = log[seq].ppMsg.pPbftBlock->vReq;
        for (uint i = 0; i < txList.size(); i++) {
            /* hard code execution result as 'y' since we are replaying tx on Bitcoin's chain. */
            completedTx.push_back(txList[i]->GetHash());
        }
        CReply reply = assembleReply(completedTx, 'y');
        connman->PushMessage(client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
        return true;
    } else {
        return false;
    }
}

void CPbft::saveBlocks2File() const {
    FILE* file = fsbridge::fopen("pbft_blocks_collab.out", "wb+");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to write." << std::endl;
        return;
    }
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);

    fileout.write((char*)&lastExecutedSeq, sizeof(lastExecutedSeq));
    for (int i = 0; i <= lastExecutedSeq; i++) {
        log[i].ppMsg.pPbftBlock->Serialize(fileout);
    }
}

int CPbft::readBlocksFromFile() {
    FILE* file = fsbridge::fopen("pbft_blocks_collab.out", "rb");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to read." << std::endl;
        return 0;
    }
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);

    int lastExecutedSeqWarmUp = 0;
    filein.read((char*)&lastExecutedSeqWarmUp, sizeof(lastExecutedSeqWarmUp));
    std::cout << __func__ << ": lastExecutedSeqWarmUp = " << lastExecutedSeqWarmUp << std::endl;
    for (int i = 0; i <= lastExecutedSeqWarmUp; i++) {
        try {
	    log[i].ppMsg.pPbftBlock = std::make_shared<CPbftBlock>();
            log[i].ppMsg.pPbftBlock->Unserialize(filein);
        }
        catch (const std::exception& e) {
            std::cerr << "Deserialize or I/O error when reading PBFT block " << i << ": " << e.what() << std::endl;
        }
    }
    return lastExecutedSeqWarmUp;
}

void CPbft::WarmUpMemoryCache() {
    CCoinsViewCache view_warmup(pcoinsTip.get());
    int lastExecutedSeqWarmUp = readBlocksFromFile();
    uint32_t nWarmUpTx = 0;
    for (int i = 0; i <= lastExecutedSeqWarmUp; i++) {
        log[i].ppMsg.pPbftBlock->Execute(i, view_warmup);
        nWarmUpTx += log[i].ppMsg.pPbftBlock->vReq.size();
        /* Discard the block to prepare for performance test. */
        log[i].ppMsg.pPbftBlock.reset();
    }
    std::cout << "warm up -- total executed tx: " << nWarmUpTx << std::endl;
}

bool CPbft::timeoutWaitReq(){
	/* log queue size if we have reached the period. */
	std::chrono::milliseconds current = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	//std::cout << "notEnoughReqStartTime  =  " << notEnoughReqStartTime.count() << ", current = " << current.count() << ", reqWaitTimeout = " << reqWaitTimeout << std::endl;
	if (notEnoughReqStartTime != std::chrono::milliseconds::zero() && current - notEnoughReqStartTime > std::chrono::milliseconds(reqWaitTimeout)) {
	    notEnoughReqStartTime = std::chrono::milliseconds::zero();
	    return true;
	} else {
	    return false;
	}
}

void CPbft::setReqWaitTimer(){
	if (notEnoughReqStartTime == std::chrono::milliseconds::zero()) {
	    notEnoughReqStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	}
}

void ThreadConsensusLogExe() {
    RenameThread("bitcoin-logexe");
    struct timeval start_process_first_block;
    while (!ShutdownRequested()) {
        bool busy = g_pbft->executeLog(start_process_first_block);
        if (!busy) {
            MilliSleep(10);
        }
    }
}

void  ThruputLogger::logServerSideThruput(struct timeval& curTime, uint32_t completedTxCnt) {
    if (completedTxCnt != 0) {
        struct timeval timeElapsed = curTime - lastLogTime;
        double thruput = (completedTxCnt - lastCompletedTxCnt) / (timeElapsed.tv_sec + timeElapsed.tv_usec * 0.000001);
        thruputSS << curTime.tv_sec << "." << curTime.tv_usec << "," <<  completedTxCnt << "," << thruput << "\n";
    } else {
        /* test just starts. */
        thruputSS << curTime.tv_sec << "." << curTime.tv_usec << ",0,0\n";
    }
    lastLogTime = curTime;
    lastCompletedTxCnt = completedTxCnt;
}

std::unique_ptr<CPbft> g_pbft;
