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

bool fIsClient; // if this node is a pbft client.
std::string leaderAddrString;
std::string clientAddrString;
int32_t pbftID; 
int32_t nMaxReqInFly; 
int32_t QSizePrintPeriod;
int32_t maxBlockSize = 2000;
int32_t nWarmUpBlocks;
bool testStarted = false;

CPbft::CPbft(): localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastExecutedSeq(-1), client(nullptr), peers(std::vector<CNode*>(groupSize)), nReqInFly(0), nCompletedTx(0), clientConnMan(nullptr), lastQSizePrintTime(std::chrono::milliseconds::zero()), totalVerifyTime(0), totalVerifyCnt(0), totalExeTime(0), lastBlockValidSeq(-1), lastBlockValidSentSeq(-1), lastReplySentSeq(-1), lastTentaExecutedSeq(-1), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    pubKeyMap.insert(std::make_pair(pbftID, myPubKey));
    pviewTenta.reset(new CCoinsViewCache(pcoinsTip.get()));
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

    /* check if at least 2f prepare has been received. If so, enter commit phase directly; 
     * otherwise, enter prepare phase.(The goal of this operation is to tolerate network
     * reordering.)
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
        std::cerr << "PBFT verification sig fail" << std::endl;
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

CReply CPbft::assembleReply(const uint32_t seq, const uint32_t idx, const char exe_res) const {
    /* 'y' --- execute sucessfully
     * 'n' --- execute fail
     */
    CReply toSent(exe_res, log[seq].ppMsg.pPbftBlock->vReq[idx]->GetHash());
    //uint256 hash;
    //toSent.getHash(hash);
    //privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}


int CPbft::executeLog(struct timeval& start_process_first_block) {
    struct timeval start_time, end_time;
    /* Step 1: sequentialy execute all slots in reply phase and verified. */
    int lastExecutedSeqStart = lastExecutedSeq;
    uint i = lastExecutedSeq + 1;
    for (; i < logSize && log[i].phase == PbftPhase::reply && log[i].blockVerified.load(std::memory_order_relaxed); i++) {
	gettimeofday(&start_time, NULL);
	log[i].txCnt = log[i].ppMsg.pPbftBlock->Execute(i, *pcoinsTip);
	gettimeofday(&end_time, NULL);
	lastExecutedSeq = i;
	nCompletedTx += log[i].txCnt;
	std::cout << "Average execution time of block " << i << ": " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / log[i].txCnt << " us/req" << std::endl; 
	logServerSideThruput(start_process_first_block, end_time, i);
    }

    /* Step 2: if the next block is ready to be processed and in our subgroup (it is not executed in step one indicates it is not verified), verify it using the real system state. */
    if (log[i].phase == PbftPhase::reply && isBlockInOurVerifyGroup(i)){
        /* This is a block to be verified by our subgroup. Verify it directly using the real system state. (The Verify call includes executing tx.) */
	gettimeofday(&start_time, NULL);
	log[i].txCnt = log[i].ppMsg.pPbftBlock->Verify(i, *pcoinsTip);
	gettimeofday(&end_time, NULL);
	lastBlockValidSeq = i;
	lastExecutedSeq = i;
	nCompletedTx += log[i].txCnt;
        /*Anounce the update-to-date verifying result to peers in the other subgroup.*/
        std::cout << "Average verify-amid-execution time of block " << i << ": " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / log[i].txCnt << " us/req" << std::endl;
	logServerSideThruput(start_process_first_block, end_time, i);
    }

    int localLastExecutedSeq = lastExecutedSeq; // use a local copy to avoid reading the volatile global copy repeatedly. 
    bool executedSomeLogSlot = localLastExecutedSeq > lastExecutedSeqStart;

    /* Step 3: verify one block belonging to our subgroup if we did not execute any blocks in Step 1 and 2.  We verify only one block per loop so that the collab msg of the other group have some time to arrive. */
    int blockIdxToBeVerified = -1;
    if (!executedSomeLogSlot) {
	for (uint j = std::max(localLastExecutedSeq, lastTentaExecutedSeq) + 1; j < logSize && log[j].phase == PbftPhase::reply; j++) {
	    if (isBlockInOurVerifyGroup(j) && !log[j].blockVerified.load(std::memory_order_relaxed)){
		blockIdxToBeVerified = j;
		break;
	    }

	}
	if (blockIdxToBeVerified > -1) {
	    /* tentative execution view. do not update system state b/c the view will be discarded. */
	    if (localLastExecutedSeq > lastTentaExecutedSeq) {
		/* The tentative view is not fresh enough, create a new tentative view from the current pcoinsTip. */
		pviewTenta.reset(new CCoinsViewCache(pcoinsTip.get()));
		lastTentaExecutedSeq = localLastExecutedSeq;
	    }
	
	    for (int j = lastTentaExecutedSeq + 1; j < blockIdxToBeVerified; j++) {
		gettimeofday(&start_time, NULL);
		log[j].txCnt = log[j].ppMsg.pPbftBlock->Execute(j, *pviewTenta);
		gettimeofday(&end_time, NULL);
		lastTentaExecutedSeq = j;
		std::cout << "Average tentative exe time of block " << j << ": " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / log[j].txCnt << " us/req" << std::endl;
	    }

	    gettimeofday(&start_time, NULL);
	    log[blockIdxToBeVerified].txCnt = log[blockIdxToBeVerified].ppMsg.pPbftBlock->Verify(blockIdxToBeVerified, *pviewTenta);
	    gettimeofday(&end_time, NULL);
	    lastBlockValidSeq = blockIdxToBeVerified;
	    log[blockIdxToBeVerified].blockVerified.store(true, std::memory_order_relaxed);
	    lastTentaExecutedSeq = blockIdxToBeVerified;
	    std::cout << "Average verify time of block " << blockIdxToBeVerified << " in my subgroup: " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / log[blockIdxToBeVerified].txCnt << " us/req" << std::endl;
	}
    }

    /* Step 4: if we have done nothing, and the next log slot is in the reply state, then we must be blocked by the other subgroup. Verify only the next block. */
    if (!executedSomeLogSlot && blockIdxToBeVerified == -1 && log[i].phase == PbftPhase::reply) {
	bool quit = false;
	gettimeofday(&start_time, NULL);
        log[i].txCnt = log[i].ppMsg.pPbftBlock->Verify(i, *pcoinsTip, &quit);
	gettimeofday(&end_time, NULL);
	lastExecutedSeq = i;
	nCompletedTx += log[i].txCnt;
	std::cout << "Average verify time of block " << i << " in the other subgroup : " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / log[i].txCnt << " us/req. quit = " << quit << std::endl;
	logServerSideThruput(start_process_first_block, end_time, i);
    }

    return lastExecutedSeq;
}

void CPbft::UpdateBlockValidity(const CCollabMessage& msg) {
    if (!checkCollabMsg(msg)) {
        return;
    }
    if (isBlockInOurVerifyGroup(msg.blockValidUpto)) {
        /* BLOCK_VALID from the peer in the same subgroup as us. Ignore it. */
        return;
    }
    std::cout << "received  BLOCK_VALID msg from peer " << msg.peerID << ", blockValidUpto = " << msg.blockValidUpto << ", current lastExecutedSeq = " << lastExecutedSeq << std::endl;
    uint32_t start_seq = isBlockInOurVerifyGroup(lastExecutedSeq + 1) ? lastExecutedSeq + 2 : lastExecutedSeq + 1;
    for (uint i = start_seq; i <= msg.blockValidUpto; i += 2) {
        if (log[i].setCollabPeerID.size() < nFaulty + 1) {
            log[i].setCollabPeerID.insert(msg.peerID);
            if (log[i].setCollabPeerID.size() == nFaulty + 1) {
                log[i].blockVerified.store(true, std::memory_order_relaxed);
                std::cout << "seq " << i << " get collab verified." << std::endl;
            }
        }
    }
}

bool CPbft::checkCollabMsg(const CCollabMessage& msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(msg.peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << msg.peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    msg.getHash(msgHash);
    if (!it->second.Verify(msgHash, msg.vchSig)) {
        std::cerr << "collab sig fail" << std::endl;
        return false;
    }
    return true;
}

bool CPbft::AssembleAndSendCollabMsg() {
    int localLastBlockValidSeq = lastBlockValidSeq; 
    if (localLastBlockValidSeq > lastBlockValidSentSeq) {
	CCollabMessage toSent; 
	toSent.blockValidUpto = localLastBlockValidSeq; 
	uint256 hash;
	toSent.getHash(hash);
	privateKey.Sign(hash, toSent.vchSig);
	toSent.sigSize = toSent.vchSig.size();

	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	uint32_t start_peerID = pbftID / CPbft::groupSize; 
	uint32_t end_peerID = start_peerID + CPbft::groupSize;

	for (uint32_t i = start_peerID; i < end_peerID; i++) {
	    if ((i ^ pbftID) & 1) {
		/* this is a peer in the other subgroup. */
		g_connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::COLLAB_BLOCK_VALID, toSent));
	    }
	} 
	lastBlockValidSentSeq = localLastBlockValidSeq; 
	return true;
    } else {
	return false;
    }
}

bool CPbft::sendReplies(CConnman* connman) {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    if (lastExecutedSeq > lastReplySentSeq) {
        /* sent reply msg for only one block per loop .*/
        int seq = ++lastReplySentSeq;
        const uint num_tx = log[seq].ppMsg.pPbftBlock->vReq.size();
	std::cout << "sending reply for block " << seq <<",  lastReplySentSeq = "<<  lastReplySentSeq << std::endl;
        for (uint i = 0; i < num_tx; i++) {
            /* hard code execution result as 'y' since we are replaying tx on Bitcoin's chain. */
            CReply reply = assembleReply(seq, i,'y');
            connman->PushMessage(client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
        }
	return true;
    } else {
	return false;
    }
}

void CPbft::saveBlocks2File(const int numBlock) const {
    FILE* file = fsbridge::fopen("pbft_blocks.out", "wb+");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to write." << std::endl;
        return;
    }
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);

    for (int i = 0; i < numBlock; i++) {
        log[i].ppMsg.pPbftBlock->Serialize(fileout);
    }
}

void CPbft::readBlocksFromFile(const int numBlock) {
    FILE* file = fsbridge::fopen("pbft_blocks.out", "rb");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to read." << std::endl;
        return;
    }
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);

    for (int i = 0; i < numBlock; i++) {
        try {
	    log[i].ppMsg.pPbftBlock = std::make_shared<CPbftBlock>();
            log[i].ppMsg.pPbftBlock->Unserialize(filein);
        }
        catch (const std::exception& e) {
            std::cerr << "Deserialize or I/O error when reading PBFT block " << i << ": " << e.what() << std::endl;
        }
    }
}

void CPbft::WarmUpMemoryCache() {
    CCoinsViewCache view_tenta(pcoinsTip.get());
    readBlocksFromFile(nWarmUpBlocks);
    for (int i = 0; i < nWarmUpBlocks; i++) {
        log[i].ppMsg.pPbftBlock->Verify(i, view_tenta);
        /* Discard the block to prepare for performance test. */
        log[i].ppMsg.pPbftBlock.reset();
    }
}

void ThreadConsensusLogExe() {
    RenameThread("bitcoin-logexe");
    struct timeval start_process_first_block = {0, 0};
    while (!ShutdownRequested()) {
        g_pbft->executeLog(start_process_first_block);
        MilliSleep(50);
    }
}

std::unique_ptr<CPbft> g_pbft;
