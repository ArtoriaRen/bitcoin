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

extern std::unique_ptr<CConnman> g_connman;

bool fIsClient; // if this node is a pbft client.
std::string leaderAddrString;
std::string clientAddrString;
int32_t pbftID; 
int32_t nMaxReqInFly; 
int32_t QSizePrintPeriod;
int32_t maxBlockSize = 2000;
bool testStarted = false;

CPbft::CPbft(): localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastExecutedSeq(-1), client(nullptr), peers(std::vector<CNode*>(groupSize)), nReqInFly(0), nCompletedTx(0), clientConnMan(nullptr), lastQSizePrintTime(std::chrono::milliseconds::zero()), totalVerifyTime(0), totalVerifyCnt(0), totalExeTime(0), lastBlockValidSeq(-1), lastBlockValidSentSeq(-1), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    pubKeyMap.insert(std::make_pair(pbftID, myPubKey));
}

ThreadSafeQueue::ThreadSafeQueue() { }

ThreadSafeQueue::~ThreadSafeQueue() { }

CMutableTxRef& ThreadSafeQueue::front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    return queue_.front();
}

std::deque<CMutableTxRef> ThreadSafeQueue::get_all() {
    std::unique_lock<std::mutex> mlock(mutex_);
    std::deque<CMutableTxRef> ret(queue_);
    queue_.clear();
    return ret;
}

std::deque<CMutableTxRef> ThreadSafeQueue::get_upto(uint32_t upto) {
    std::unique_lock<std::mutex> mlock(mutex_);
    if (queue_.size() < upto) {
	std::deque<CMutableTxRef> ret(queue_);
	queue_.clear();
	return ret;
    } else {
	std::deque<CMutableTxRef> ret;
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

void ThreadSafeQueue::push_back(const CMutableTxRef& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

void ThreadSafeQueue::push_back(CMutableTxRef&& item) {
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
    ppMsg.pbft_block.ComputeHash();
    if (ppMsg.digest != ppMsg.pbft_block.hash) {
	std::cerr << "digest does not match block hash tx. block hash = " << ppMsg.pbft_block.hash.GetHex() << ", but digest = " << ppMsg.digest.GetHex() << std::endl;
	return false;
    }
    // add to log
    log[ppMsg.seq].ppMsg = ppMsg;

    /* check if at least 2f prepare has been received. If so, enter commit phase directly; 
     * otherwise, enter prepare phase.(The goal of this operation is to tolerate network
     * reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */

    std::cout << "digest = " << ppMsg.digest.GetHex() << std::endl;

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
        std::cout << "enter reply phase" << std::endl;
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
        std::cerr << "verification sig fail" << std::endl;
        return false;
    }
    std::cerr << "sig ok" << std::endl;
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

CPre_prepare CPbft::assemblePPMsg(const CPbftBlock& pbft_block) {
    CPre_prepare toSent; // phase is set to Pre_prepare by default.
    toSent.seq = nextSeq++;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.pbft_block = pbft_block;
    toSent.digest = toSent.pbft_block.hash;
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

CReply CPbft::assembleReply(const uint32_t seq, const uint32_t idx, const char exe_res) {
    /* 'y' --- execute sucessfully
     * 'n' --- execute fail
     */
    CReply toSent(exe_res, log[seq].ppMsg.pbft_block.vReq[idx]->GetHash());
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

int CPbft::executeLog() {
    /* Step 1: execute all lower-seq tx until this one if possible. */
    CCoinsViewCache view(pcoinsTip.get());
    uint i = lastExecutedSeq + 1;
    /* We should go on to execute all log slots that are in reply phase even
     * their seqs are greater than the seq passed in. If we only execute up to
     * the seq passed in, a slot missing a pbftc msg might permanently block
     * log slots after it to be executed. */
    for (; i < logSize; i++) {
        if (log[i].phase == PbftPhase::reply && log[i].blockVerified) {
	    log[i].txCnt = log[i].ppMsg.pbft_block.Execute(i, g_connman.get(), view);
	    nCompletedTx += log[i].txCnt;
	    std::cout << "Average execution time: " << totalExeTime/nCompletedTx 
                    << " us/req" << ", current total completed tx = " << nCompletedTx 
		    << ". Executed block " << log[i].ppMsg.digest.GetHex() 
                    << " at log slot = " << i  << ", block size = " 
                    << log[i].ppMsg.pbft_block.vReq.size()  
                    << std::endl;
            /* wake up the client-listening thread to send results to clients. The 
             * client-listening thread is probably already up if the client sends 
             * request too frequently. 
             * The following code cause linking errors still not fixed.
             */
            if (isLeader()) {
                clientConnMan->WakeMessageHandler();
            }
        } else {
            break;
        } 
    }
    bool flushed = view.Flush(); // flush to pcoinsTip
    assert(flushed);
    bool executedSomeLogSlot = i - 1 > lastExecutedSeq;
    lastExecutedSeq = i - 1;

    /* Step 2: verify blocks belonging to our subgroup. */
    int lastBlockValidSeqStart = lastBlockValidSeq;
    /* tentative execution view. do not update system state b/c the view will be discarded. */
    CCoinsViewCache view_tenta(pcoinsTip.get());
    for (uint j = lastExecutedSeq + 1; j < logSize && log[j].phase == PbftPhase::reply; j++) {
        if (isBlockInOurVerifyGroup(j)) {
            std::cout << "verifying block " << j << " in my subgroup" << std::endl;
            totalVerifyCnt += log[j].ppMsg.pbft_block.Verify(j, view_tenta);
            log[j].blockVerified = true;
            lastBlockValidSeq = j;
	    std::cout << "Average verifying time: " << totalVerifyTime/totalVerifyCnt 
                    << " us/req" << std::endl;
        } else {
	    log[j].ppMsg.pbft_block.Execute(j, nullptr, view_tenta);
	}
	std::cout << "lastExecutedSeq  = " << lastExecutedSeq  << ", lastBlockValidSeq =  " << lastBlockValidSeq << std::endl;
    }

    if (lastBlockValidSeq != lastBlockValidSeqStart) {
        /* we have verified at least one block in our subgroup. Anounce the update-to-date 
         * verifying result to peers in the other subgroup. */
        AssembleAndSendCollabMsg();
    }
    
    /* Step 3: if we did have done nothing, and the next log slot is in the reply state, then we
     * must be blocked by the other subgroup. Verify only the next block. */
    if (!executedSomeLogSlot && lastBlockValidSeq == lastBlockValidSeqStart && log[lastExecutedSeq + 1].phase == PbftPhase::reply) {
	std::cout << " verifying block " << lastExecutedSeq + 1 << " belonging to the other subgroup."<< std::endl;
	CCoinsViewCache view_verify(pcoinsTip.get());
        log[i].ppMsg.pbft_block.Verify(i, view_verify);
        log[i].blockVerified = true;
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
                log[i].blockVerified = true;
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

void CPbft::AssembleAndSendCollabMsg() {
    CCollabMessage toSent; 
    if (lastBlockValidSeq > lastBlockValidSentSeq) {
        toSent.blockValidUpto = lastBlockValidSeq; 
        lastBlockValidSentSeq = lastBlockValidSeq;
    } else {
        toSent.blockValidUpto = -1; 
    }

    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    uint32_t start_peerID = pbftID / CPbft::groupSize; // skip the leader id b/c it is myself
    uint32_t end_peerID = start_peerID + CPbft::groupSize;
    for (uint32_t i = start_peerID; i < end_peerID; i++) {
        if ((i ^ pbftID) & 1) {
            /* this is a peer in the other subgroup. */
            g_connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::COLLAB_BLOCK_VALID, toSent));
        }
    }
}

void ThreadConsensusLogExe() {
    RenameThread("bitcoin-logexe");
    while (!ShutdownRequested()) {
        g_pbft->executeLog();
        MilliSleep(50);
    }
}

std::unique_ptr<CPbft> g_pbft;
