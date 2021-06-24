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
struct timeval collabResWaitTime = {60, 0};
size_t groupSize = 4;
uint32_t nFaulty = 1;
uint32_t vrfResBatchSize = 500;
volatile bool waitAllblock = false;

CPbft::CPbft(): localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastConsecutiveSeqInReplyPhase(-1), client(nullptr), peers(std::vector<CNode*>(groupSize)), nReqInFly(0), nCompletedTx(0), clientConnMan(nullptr), lastQSizePrintTime(std::chrono::milliseconds::zero()), totalVerifyTime(0), totalVerifyCnt(0), totalExeTime(0), lastBlockVerifiedThisGroup(-1), qValidTx(2), qInvalidTx(2), validTxQIdx(0), invalidTxQIdx(0), otherSubgroupSendQ(groupSize), notEnoughReqStartTime(std::chrono::milliseconds::zero()), qNotInitialExecutedTx(2), qExecutedTx(2), notExecutedQIdx(0), executedQIdx(0), qCollabMsg(2), qCollabMulBlkMsg(2), collabMsgQIdx(0), collabMulBlkMsgQIdx(0), nTxSentByLeader(0), nWarmUpTx(0), privateKey(CKey()) {
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

bool CPbft::ProcessPP(CConnman* connman, CPbftMessage& ppMsg) {
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if (!checkMsg(&ppMsg)) {
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
        /* update greatest consecutive seq in reply phase. */
        while (log[lastConsecutiveSeqInReplyPhase + 1].phase == PbftPhase::reply) {
            lastConsecutiveSeqInReplyPhase++;
        }

        if (cMsg.seq == 0) {
            /* log that test has started. */
            struct timeval curTime;
            gettimeofday(&curTime, NULL);
        }

        /* the log-exe thread will execute blocks in reply phase sequentially. */
    }
    return true;
}

bool CPbft::checkBlkMsg(CBlockMsg& msg) {
    auto it = pubKeyMap.find(msg.peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << msg.peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    msg.getHash(msgHash);
    if (!it->second.Verify(msgHash, msg.vchSig)) {
        std::cerr << "PBFT verification sig fail" << std::endl;
        return false;
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

CBlockMsg CPbft::assembleBlkMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn, uint32_t seq) {
    CBlockMsg toSent(pPbftBlockIn, seq); // phase is set to Pre_prepare by default.
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so client tx is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CPbftMessage CPbft::assemblePPMsg(uint256& block_hash) {
    CPbftMessage toSent; // phase is set to Pre_prepare by default.
    toSent.seq = nextSeq++;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = block_hash;
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

void CPbft::informReplySendingThread(uint32_t height, std::deque<uint32_t>& qDependentTx) {
    /* inform the reply sending thread of what tx is not executed. */
    mutex4ExecutedTx.lock();
    qNotInitialExecutedTx[notExecutedQIdx].emplace_back(height, std::move(qDependentTx));
    mutex4ExecutedTx.unlock();
}

bool CPbft::executeLog(struct timeval& start_process_first_block) {
    /* 1. Verify and execute prereq-clear tx in sequece and in the granularty 
     * of blocks for blocks belonging to our subgroup.
     * 2. Execute COLLAB_VERIFIED tx. If some tx belonging to our subgroup 
     * becomes prerequisite, verifiy and execute them.
     * 3. If there is nothing to be verified in Steps 1 or 2, verify the first 
     * not yet verified block belonging to the other subgroup (in the granularity
     * of tx). And remove them from the dependency graph.  
     */
    bool doneSomething = false;
    struct timeval start_time, end_time;
    /* valid and invalid tx array for assembling possible collabMulBlk msg.*/
    std::vector<TxIndexOnChain> validTxsMulBlk;
    std::vector<TxIndexOnChain> invalidTxsMulBlk;
    /* Step 1: verify the successor block of the last block we have verified
     * if it belongs to our group. Otherwise, use collab_vrf result to execute
     * tx in the block. */
    uint32_t nextHeight = lastBlockVerifiedThisGroup + 1;
    if (lastBlockVerifiedThisGroup < lastConsecutiveSeqInReplyPhase
            && log[nextHeight].pPbftBlock != nullptr){
        /* the block hash should match PBFT pre-prepare msg. */
        assert(log[nextHeight].pPbftBlock->hash == log[nextHeight].ppMsg.digest);
        doneSomething = true;
        uint32_t curHeight = lastBlockVerifiedThisGroup + 1;
        std::cout << "sequentially process block " << curHeight << ": ";
        CPbftBlock& block = *log[curHeight].pPbftBlock;
        if(isInVerifySubGroup(pbftID, curHeight)) {
            /* This is a block to be verified by our subgroup. 
             * Verify and Execute tx in this block.
             * (The VerifyTx call includes executing tx.) 
             */
            struct timeval totalVrfTime = {0, 0};
            //struct timeval collabMsgSendingTime = {0, 0};
            gettimeofday(&start_time, NULL);
            std::cout << "verifying block " << curHeight << " of size " << block.vReq.size() << std::endl;
            uint32_t validTxCnt = block.Verify(curHeight, *pcoinsTip);
            gettimeofday(&end_time, NULL);
            totalVrfTime += end_time - start_time;
            lastBlockVerifiedThisGroup++;
            nCompletedTx += validTxCnt;
            //std::cout << "valid tx cnt = " << validTxCnt << ". invalid tx cnt = " << invalidTxs.size() << std::endl;
        } else {
            /* This is a block of the other subgroup.
             * Check if we have collab_vrf results for this block. If so, execute
             * valid tx, and add not yet verified tx to the dependency graph.
             */
            struct timeval totalExeTime = {0, 0};
            struct timeval totalDependencyCheckTime = {0, 0};
            struct timeval totalAdd2GraphTime = {0, 0};
            if (futureCollabVrfedBlocks.find(curHeight) != futureCollabVrfedBlocks.end()) {
                /* a queue of tx that are not executed b/c dependency. */
                std::deque<uint32_t> notExecutedTxs; 
                uint32_t localExecutedTxCnt = 0;
                //std::cout << " future map has block "  << curHeight << std::endl;
                for (uint i = 0; i < block.vReq.size(); i++) {
                    CTransactionRef pTx =  block.vReq[i];
                    if (futureCollabVrfedBlocks[curHeight][i] == 1) {
                        /* valid tx */
                        /* execution may fail due to missing inputs. */
                        if (ExecuteTx(*pTx, curHeight, *pcoinsTip)) {
                            localExecutedTxCnt++;
                        }
                    } else if (futureCollabVrfedBlocks[curHeight][i] == 0) {
                        /* not-yet-verified tx */
                        notExecutedTxs.push_back(i);
                    } /* for invalid tx, there is nothing to do because the tx is not
                       * yet added to the dependency graph. */
                }
                gettimeofday(&end_time, NULL);
                futureCollabVrfedBlocks.erase(curHeight);
                informReplySendingThread(curHeight, notExecutedTxs);
                nCompletedTx += localExecutedTxCnt;
            } 
            /*Although we did not verified tx in this block, we advance the 
             * pointer so that we would not check the same block next time. */
            lastBlockVerifiedThisGroup++;
        }
    }

    /* Step 2: Process collab_vrf results. For block heights less than or equal to
     * lastBlockVerifiedThisGroup, execute the tx immediately. Otherwise, store the
     * collab_vrf result in the futureCollabVrfedBlocks map.
     */
    if (!qValidTx[validTxQIdx].empty()) {
        doneSomething = true;
        std::deque<TxIndexOnChain> localQExecutedTx;
        for (const TxIndexOnChain& txIdx: qValidTx[validTxQIdx]) {
            if ((int) txIdx.block_height > lastBlockVerifiedThisGroup) {
                if (futureCollabVrfedBlocks.find(txIdx.block_height) == futureCollabVrfedBlocks.end()) {
                    /* we haven't met collab result for any tx in this block, create 
                     * a new entry for this block.
                     */
                    futureCollabVrfedBlocks.emplace(txIdx.block_height, std::deque<char>(log[txIdx.block_height].blockSizeInCollabMsg));
                }
                futureCollabVrfedBlocks[txIdx.block_height][txIdx.offset_in_block] = 1;
            } else {
                /* backlog collab res, execute tx.*/
                CTransactionRef pTx = log[txIdx.block_height].pPbftBlock->vReq[txIdx.offset_in_block];
                if(ExecuteTx(*pTx, txIdx.block_height, *pcoinsTip)) {
                    localQExecutedTx.push_back(std::move(txIdx));
                    nCompletedTx += 1;
                }

            }
        }
        gettimeofday(&end_time, NULL);
        qValidTx[validTxQIdx].clear();

        /* inform the reply sending thread of what tx has been executed. */
        mutex4ExecutedTx.lock();
        qExecutedTx[executedQIdx].insert(qExecutedTx[executedQIdx].end(), localQExecutedTx.begin(), localQExecutedTx.end());
        mutex4ExecutedTx.unlock();
        /* TODO : handle invalid tx queue. */
    }

    /* check if the pointers of queue pairs should be switched. */
    if (mutex4Q.try_lock()) {
        /* switch the queues if the queue used by net_processing thread has some tx. */
        if (!qValidTx[1 - validTxQIdx].empty()) {
            validTxQIdx = 1 - validTxQIdx; 
        }
        if (!qInvalidTx[1 - invalidTxQIdx].empty()) {
            invalidTxQIdx = 1 - invalidTxQIdx; 
        }
        mutex4Q.unlock();
    }

     /* return true if we have done something useful. */
     return doneSomething || !qValidTx[validTxQIdx].empty() || !qInvalidTx[invalidTxQIdx].empty();
}

bool CPbft::checkCollabMsg(const CCollabMessage& msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(msg.peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "checkCollabMsg: no pub key for sender " << msg.peerID << std::endl;
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

bool CPbft::checkCollabMulBlkMsg(const CCollabMultiBlockMsg& msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(msg.peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "checkCollabMsg: no pub key for sender " << msg.peerID << std::endl;
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

bool CPbft::SendCollabMsg() {
    if (qCollabMsg[1 - collabMsgQIdx].empty()) {
        /* swap queue with the log-exe thread if possible. */
        bool qEmpty = true;
        if (mutexCollabMsgQ.try_lock()) {
            /* switch the queues if the queue used by log-exe thread has some collabMsg. */
            if (!qCollabMsg[collabMsgQIdx].empty()) {
                collabMsgQIdx = 1 - collabMsgQIdx; 
                qEmpty = false;
            }
            mutexCollabMsgQ.unlock();
            if (qEmpty)
                return false;
        } else {
            return false;
        }
    }
    for (CCollabMessage& toSent: qCollabMsg[1 - collabMsgQIdx]) {
        uint256 hash;
        toSent.getHash(hash);
        privateKey.Sign(hash, toSent.vchSig);
        toSent.sigSize = toSent.vchSig.size();
        //std::cout << "sending collab msg for block " << toSent.height << std::endl;
        const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
        /* the receiving peers have been calculated when we heard about the block */
        for (auto peerId: mapBlockOtherSubgroup[toSent.height]) {
            g_connman->PushMessage(peers[peerId], msgMaker.Make(NetMsgType::COLLAB_VRF, toSent));
        }
    }
    qCollabMsg[1 - collabMsgQIdx].clear();
    return true;
}

bool CPbft::SendCollabMultiBlkMsg() {
    if (qCollabMulBlkMsg[1 - collabMulBlkMsgQIdx].validTxs.empty()
            && qCollabMulBlkMsg[1 - collabMulBlkMsgQIdx].invalidTxs.empty()) {
        /* swap queue with the log-exe thread if possible. */
        bool qEmpty = true;
        if (mutexCollabMsgQ.try_lock()) {
            /* switch the queues if the queue used by log-exe thread has some collabMsg. */
            if (!qCollabMulBlkMsg[collabMulBlkMsgQIdx].validTxs.empty() || !qCollabMulBlkMsg[collabMulBlkMsgQIdx].invalidTxs.empty()) {
                collabMulBlkMsgQIdx = 1 - collabMulBlkMsgQIdx; 
                qEmpty = false;
            }
            mutexCollabMsgQ.unlock();
            if (qEmpty)
                return false;
        } else {
            return false;
        }
    }

    for (const TxIndexOnChain& tx: qCollabMulBlkMsg[1 - collabMulBlkMsgQIdx].validTxs) {
        //std::cout << "sending multiBlkCollabMsg of tx " << tx.ToString() << " to peer ";
        for (auto peerId: mapBlockOtherSubgroup[tx.block_height]) {
            //std::cout << peerId << ", ";
            otherSubgroupSendQ[peerId].validTxs.push_back(tx);
        }
        //std::cout << std::endl;
    }
    qCollabMulBlkMsg[1 - collabMulBlkMsgQIdx].validTxs.clear();
    for (const TxIndexOnChain& tx: qCollabMulBlkMsg[1 - collabMulBlkMsgQIdx].invalidTxs) {
        for (auto peerId: mapBlockOtherSubgroup[tx.block_height]) {
            otherSubgroupSendQ[peerId].invalidTxs.push_back(tx);
        }
    }
    qCollabMulBlkMsg[1 - collabMulBlkMsgQIdx].invalidTxs.clear();

    /* send multiBlkCollabMsg. clear the sending queue. */
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    for (uint i = 0; i < groupSize; i++) {
        if (i == pbftID || otherSubgroupSendQ[i].empty())
            continue;

        CCollabMultiBlockMsg& toSent = otherSubgroupSendQ[i];
        uint256 hash;
        toSent.getHash(hash);
        privateKey.Sign(hash, toSent.vchSig);
        toSent.sigSize = toSent.vchSig.size();
        g_connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::COLLAB_MULTI_BLK, toSent));
        toSent.clear();
    }

    return true;
}

bool CPbft::sendReplies(CConnman* connman) {
    if (qNotInitialExecutedTx[1 - notExecutedQIdx].empty() && qExecutedTx[1 - executedQIdx].empty()) {
        /* swap queue with the log-exe thread if possible. */
        bool bothQEmpty = true;
        if (mutex4ExecutedTx.try_lock()) {
            /* switch the queues if the queue used by log-exe thread has some tx. */
            if (!qNotInitialExecutedTx[notExecutedQIdx].empty()) {
                notExecutedQIdx = 1 - notExecutedQIdx; 
                bothQEmpty = false;
            }
            if (!qExecutedTx[executedQIdx].empty()) {
                executedQIdx = 1 - executedQIdx; 
                bothQEmpty = false;
            }
            mutex4ExecutedTx.unlock();
            if (bothQEmpty)
                return false;
        } else {
            return false;
        }
    }

    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    std::deque<uint256> completedTx;
    for (InitialBlockExecutionStatus& p: qNotInitialExecutedTx[1 - notExecutedQIdx]) {
        /* send reply for all tx in the block except for tx in the InitialExecutedTx list. */
        std::vector<CTransactionRef>& txList = log[p.height].pPbftBlock->vReq;
        for (uint i = 0; i < txList.size(); i++) {
            if (!p.dependentTxs.empty() && i == p.dependentTxs.front()) {
                p.dependentTxs.pop_front();
            } else {
                completedTx.push_back(txList[i]->GetHash());
            }
        }
    } 
    qNotInitialExecutedTx[1 - notExecutedQIdx].clear();

    for (const TxIndexOnChain& txIdx: qExecutedTx[1 - executedQIdx]) {
        completedTx.push_back(log[txIdx.block_height].pPbftBlock->vReq[txIdx.offset_in_block]->GetHash());
    }
    qExecutedTx[1 - executedQIdx].clear();

    CReply reply = assembleReply(completedTx, 'y');
    connman->PushMessage(client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));

    return true;
}

BlockCollabRes::BlockCollabRes(): collab_msg_full_tx_cnt(0) { }

BlockCollabRes::BlockCollabRes(uint32_t txCnt): tx_collab_valid_cnt(txCnt), collab_msg_full_tx_cnt(0) { }

void CPbft::UpdateTxValidity(const CCollabMessage& msg) {
    if (!checkCollabMsg(msg)) {
        std::cerr << "collab msg invalid" << std::endl;
        return;
    }

    std::deque<TxIndexOnChain> localValidTxQ;
    std::deque<TxIndexOnChain> localInvalidTxQ;
    if (mapBlockCollabRes.find(msg.height) == mapBlockCollabRes.end()) {
        if (log[msg.height].pPbftBlock == nullptr) {
            log[msg.height].blockSizeInCollabMsg = msg.txCnt;
        }
        else {
            log[msg.height].blockSizeInCollabMsg = log[msg.height].pPbftBlock->vReq.size();
        }
        mapBlockCollabRes.emplace(msg.height, BlockCollabRes(log[msg.height].blockSizeInCollabMsg));
        //std::cout << "create collab msg counters for block "<< msg.height << ", tx count = " << log[msg.height].pPbftBlock->vReq.size() << std::endl;
    } else if (mapBlockCollabRes[msg.height].tx_collab_valid_cnt.size() > msg.txCnt) {
        /*This entry is created when we received CollabMulti msg for this block. The block size is in accurate, revise it here.*/
        if (log[msg.height].pPbftBlock == nullptr) {
            log[msg.height].blockSizeInCollabMsg = msg.txCnt;
        }
        else {
            log[msg.height].blockSizeInCollabMsg = log[msg.height].pPbftBlock->vReq.size();
        }
        mapBlockCollabRes[msg.height].tx_collab_valid_cnt.resize(log[msg.height].blockSizeInCollabMsg);
    }

    
    BlockCollabRes& block_collab_res = mapBlockCollabRes[msg.height];

    if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
        /* all tx in this block has accumlated enough collab_verify res. ignore this msg.*/
        return;
    }

    for (const auto txSeq: msg.validTxs) {
            block_collab_res.tx_collab_valid_cnt[txSeq]++;
            //std::cout << "Collab msg from peer " << msg.peerID << "tx (" << msg.height << ", " << txSeq << ") is valid. vrf res cnt = " << block_collab_res.tx_collab_valid_cnt[txSeq] << std::endl;
            if (block_collab_res.tx_collab_valid_cnt[txSeq] == nFaulty + 1) {
                block_collab_res.collab_msg_full_tx_cnt++;
                /* add the tx to validTxQ */
                localValidTxQ.emplace_back(msg.height, txSeq);
            }
    }

    for (const auto txSeq: msg.invalidTxs) {
        block_collab_res.map_collab_invalid_cnt[txSeq]++;
        if (block_collab_res.map_collab_invalid_cnt[txSeq] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to inValidTxQ */
            localInvalidTxQ.emplace_back(msg.height, txSeq);
        }
    }

    //std::cout << "processed collab msg for block " << msg.height << " from peer " << msg.peerID << ". valid tx cnt = " << msg.validTxs.size() << ", invalid tx cnt = " << msg.invalidTxs.size() << std::endl;

    /* prune entries in mapBlockCollabRes.
     * All blocks meeting the following two conditions can be removed:
     * 1) have height lower than < lastBlockVerifiedThisGroup 
     * 2) whose tx are all fully collab-verified 
     */
    /* add tx in local queues to the global queues*/
    mutex4Q.lock();
    qValidTx[1 - validTxQIdx].insert(qValidTx[1 - validTxQIdx].end(), localValidTxQ.begin(),  localValidTxQ.end());
    qInvalidTx[1 - invalidTxQIdx].insert(qInvalidTx[1 - invalidTxQIdx].end(), localInvalidTxQ.begin(), localInvalidTxQ.end());
    mutex4Q.unlock();

}

void CPbft::UpdateTxValidity(const CCollabMultiBlockMsg& msg) {
    //std::cout << "received collabMulBlk msg from peer " << msg.peerID << std::endl;
    if (!checkCollabMulBlkMsg(msg)) {
        std::cerr << "collabMulti msg invalid" << std::endl;
        return;
    }

    std::deque<TxIndexOnChain> localValidTxQ;
    std::deque<TxIndexOnChain> localInvalidTxQ;
    /* add valid tx to the BlockCollabRes map*/
    for (TxIndexOnChain txIdx: msg.validTxs) {
        if (mapBlockCollabRes.find(txIdx.block_height) == mapBlockCollabRes.end()) {
            if (log[txIdx.block_height].pPbftBlock == nullptr) {
                log[txIdx.block_height].blockSizeInCollabMsg = maxBlockSize;
            }
            else {
                log[txIdx.block_height].blockSizeInCollabMsg = log[txIdx.block_height].pPbftBlock->vReq.size();
            }
            mapBlockCollabRes.emplace(txIdx.block_height, BlockCollabRes(log[txIdx.block_height].blockSizeInCollabMsg));

        //std::cout << "create collab msg counters for block "<< msg.height << ", tx count = " << log[msg.height].pPbftBlock->vReq.size() << std::endl;
    }

        /* because CollabMulBlkMsg contains only info for previous blockly verified tx,
         * the entry for the block must exist in  mapBlockCollabRes. */
        BlockCollabRes& block_collab_res = mapBlockCollabRes[txIdx.block_height];

        if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
            /* all tx in this block has accumlated enough collab_verify res. ignore this tx.
             * This block is not pruned yet because we prune blocks consequtively.
             */
            std::cout << __func__ << ": vrf res full block. tx cnt = " << block_collab_res.tx_collab_valid_cnt.size() << std::endl;
            continue;
        }

        block_collab_res.tx_collab_valid_cnt[txIdx.offset_in_block]++;
        if (block_collab_res.tx_collab_valid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to validTxQ */
            localValidTxQ.push_back(std::move(txIdx));
        }
    }

    /* add invalid tx to the BlockCollabRes map*/
    for (TxIndexOnChain txIdx: msg.invalidTxs) {
        BlockCollabRes& block_collab_res = mapBlockCollabRes[txIdx.block_height];
        block_collab_res.map_collab_invalid_cnt[txIdx.offset_in_block]++;
        if (block_collab_res.map_collab_invalid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to inValidTxQ */
            localInvalidTxQ.push_back(std::move(txIdx));
        }
    }

    //std::cout << "processed collabMulBlk msg from peer " << msg.peerID << ", has " << msg.validTxs.size() << " valid tx, " <<  msg.invalidTxs.size() << " invalid tx" << std::endl;

    /* add tx in local queues to the global queues*/
    mutex4Q.lock();
    qValidTx[1 - validTxQIdx].insert(qValidTx[1 - validTxQIdx].end(), localValidTxQ.begin(),  localValidTxQ.end());
    qInvalidTx[1 - invalidTxQIdx].insert(qInvalidTx[1 - invalidTxQIdx].end(), localInvalidTxQ.begin(), localInvalidTxQ.end());
    mutex4Q.unlock();

}


void CPbft::saveBlocks2File() const {
    FILE* file = fsbridge::fopen("pbft_blocks_collab.out", "wb+");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to write." << std::endl;
        return;
    }
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);

    fileout.write((char*)&lastConsecutiveSeqInReplyPhase, sizeof(lastConsecutiveSeqInReplyPhase));
    for (int i = 0; i <= lastConsecutiveSeqInReplyPhase; i++) {
        log[i].pPbftBlock->Serialize(fileout);
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
	    log[i].pPbftBlock = std::make_shared<CPbftBlock>();
            log[i].pPbftBlock->Unserialize(filein);
        }
        catch (const std::exception& e) {
            std::cerr << "Deserialize or I/O error when reading PBFT block " << i << ": " << e.what() << std::endl;
        }
    }
    return lastExecutedSeqWarmUp;
}

void CPbft::WarmUpMemoryCache() {
    /*may not need warm up anymore if using SSD, but still, they are slower
     * than memory. The goal of warm up is to load UTXOs to be spent into memory,
     * and this match the practical use case. */
    CCoinsViewCache view_warmup(pcoinsTip.get());
    int lastExecutedSeqWarmUp = readBlocksFromFile();
    nWarmUpTx = 0;
    for (int i = 0; i <= lastExecutedSeqWarmUp; i++) {
        log[i].pPbftBlock->Execute(i, view_warmup);
        nWarmUpTx += log[i].pPbftBlock->vReq.size();
        /* Discard the block to prepare for performance test. */
        log[i].pPbftBlock.reset();
    }
    std::cout << "warm up -- total executed tx: " << nWarmUpTx << std::endl;
}

void ThreadConsensusLogExe() {
    RenameThread("bitcoin-logexe");
    struct timeval start_process_first_block = {0, 0};
    while (waitAllblock) {
        MilliSleep(10);
    }
    while (!ShutdownRequested()) {
        bool busy = g_pbft->executeLog(start_process_first_block);
        if (!busy) {
            MilliSleep(10);
        }
    }
}

TxIndexOnChain::TxIndexOnChain(): block_height(0), offset_in_block(0) { }

TxIndexOnChain::TxIndexOnChain(uint32_t block_height_in, uint32_t offset_in_block_in):
 block_height(block_height_in), offset_in_block(offset_in_block_in) { }

bool TxIndexOnChain::IsNull() {
    return block_height == 0 && offset_in_block == 0;
}

bool operator<(const TxIndexOnChain& a, const TxIndexOnChain& b)
{
    return a.block_height < b.block_height || 
	    (a.block_height == b.block_height && a.offset_in_block < b.offset_in_block);
}

bool operator>(const TxIndexOnChain& a, const TxIndexOnChain& b)
{
    return a.block_height > b.block_height || 
	    (a.block_height == b.block_height && a.offset_in_block > b.offset_in_block);
}

bool operator==(const TxIndexOnChain& a, const TxIndexOnChain& b) {
    return a.block_height == b.block_height && a.offset_in_block == b.offset_in_block;
}

bool operator!=(const TxIndexOnChain& a, const TxIndexOnChain& b) {
    return ! (a == b);
}

bool operator<=(const TxIndexOnChain& a, const TxIndexOnChain& b) {
    return a.block_height < b.block_height || 
	    (a.block_height == b.block_height && a.offset_in_block <= b.offset_in_block);
}

TxIndexOnChain TxIndexOnChain::operator+(const unsigned int oprand) {
    const unsigned int cur_block_size = chainActive[block_height]->nTx;
    if (offset_in_block + oprand < cur_block_size) {
	return TxIndexOnChain(block_height, offset_in_block + oprand);
    } else {
	uint32_t cur_block = block_height + 1;
	uint32_t cur_oprand = oprand - (cur_block_size - offset_in_block);
	while (cur_oprand >= chainActive[cur_block]->nTx) {
	    cur_oprand -= chainActive[cur_block]->nTx;
	    cur_block++;
	}
	return TxIndexOnChain(cur_block, cur_oprand);
    }
}

std::string TxIndexOnChain::ToString() const {
    return "(" + std::to_string(block_height) + ", " + std::to_string(offset_in_block) + ")";
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

PendingTxStatus::PendingTxStatus(): remaining_prereq_tx_cnt(0), collab_status(0) { }
PendingTxStatus::PendingTxStatus(uint32_t remaining_prereq_tx_cnt_in, char collab_status_in): remaining_prereq_tx_cnt(remaining_prereq_tx_cnt_in), collab_status(collab_status_in) { }

InitialBlockExecutionStatus::InitialBlockExecutionStatus(){ };
InitialBlockExecutionStatus::InitialBlockExecutionStatus(uint32_t heightIn, std::deque<uint32_t>&& dependentTxsIn): height(heightIn), dependentTxs(dependentTxsIn){ };

void  ThruputLogger::logServerSideThruput(struct timeval& curTime, uint32_t completedTxCnt) {
    if (lastLogTime.tv_sec == 0) {
        lastLogTime = curTime;
        lastCompletedTxCnt = completedTxCnt;
        return;
    }
    struct timeval timeElapsed = curTime - lastLogTime;
    /* log throughput per second. */
    if (timeElapsed.tv_sec * 1000000 + timeElapsed.tv_usec < 1000000) {
        return;
    }
    double thruput = (completedTxCnt - lastCompletedTxCnt) / (timeElapsed.tv_sec + timeElapsed.tv_usec * 0.000001);
    thruputSS << curTime.tv_sec << "." << curTime.tv_usec << "," << completedTxCnt << "," << thruput << "\n";
    lastLogTime = curTime;
    lastCompletedTxCnt = completedTxCnt;
}

void CPbft::computeVG(const uint256& block_hash, const uint32_t height){
    std::map<uint, uint> mapHashValues;
    CHash256 hasher;
    uint256 result;
    for (uint i = 0; i < groupSize; i++) {
        hasher.Write((const unsigned char*)block_hash.begin(), block_hash.size())
                .Finalize((unsigned char*)&result);
        mapHashValues.emplace(result.GetCheapHash(), i);
    }
    std::map<uint, uint>::iterator it = mapHashValues.begin();
    bool isInVGofThisBlock = false;
    for (uint i = 0; i < nFaulty + 1; i++) {
        log[height].vrfGroup.insert(it->second);
        if (!isInVGofThisBlock && it->second == pbftID) {
            isInVGofThisBlock = true;
        }
        it++;
    }

    /* fill the map about where to send vrf results. */
    if (isInVGofThisBlock) {
        for (; it != mapHashValues.end(); it++) {
            mapBlockOtherSubgroup[height].push_back(it->second);
        }
        assert(mapBlockOtherSubgroup[height].size() == groupSize - (nFaulty + 1));
    }

    /* replace the PBFT leader peer with the last-ranked peer. */
    it = mapHashValues.begin();
    for (uint i = 0; i < groupSize - 1; i++, it++) {
        if (it->second % groupSize == 0) {
            /* this is the PBFT leader node, replace it with the last node. */
            it->second = mapHashValues.rbegin()->second;
            break;
        }
    }

    if (isLeader()) {
        /* leader should send block to the first peer in VG. */
        log[height].successorBlockPassing = mapHashValues.begin()->second;
    } else {
        /* the peer ranked next to us is the successor of block propagation. */
        it = mapHashValues.begin();
        for (uint i = 0; i < groupSize - 2 ; i++, it++) {
            if (it->second == pbftID) {
                log[height].successorBlockPassing = (++it)->second;
                break;
            }
        }
    }

    std::string toPrint("peers' hash ranking for block " + std::to_string(height) + " is:"); 
    for (const auto& entry : mapHashValues) {
        toPrint += std::to_string(entry.second) + ",";
    }
    toPrint += " successor = " + std::to_string(log[height].successorBlockPassing);
    std::cout << toPrint << std::endl;
}

bool CPbft::isInVerifySubGroup(int32_t peer_id, const uint32_t height){
    assert (!log[height].vrfGroup.empty());
    return log[height].vrfGroup.find(peer_id) != log[height].vrfGroup.end();
}

void CPbft::Copy2CollabMsgQ(uint32_t block_height, uint32_t block_size, std::vector<uint32_t>& validTxs, std::vector<uint32_t>& invalidTxs) {
    mutexCollabMsgQ.lock();
    qCollabMsg[collabMsgQIdx].emplace_back(block_height, block_size, validTxs, invalidTxs);
    mutexCollabMsgQ.unlock();
}

ThruputLogger::ThruputLogger(): lastLogTime({0, 0}) { }

std::unique_ptr<CPbft> g_pbft;
