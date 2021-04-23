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
#include "consensus/validation.h"
#include "consensus/params.h"
#include "consensus/tx_verify.h"
#include "coins.h"
#include "script/interpreter.h"
#include "undo.h"
#include "netmessagemaker.h"
#include <memory>
#include "tx_placement/tx_placer.h"
#include "netmessagemaker.h"
#include "streams.h"
#include "clientversion.h"
#include "init.h"

extern std::unique_ptr<CConnman> g_connman;

bool fIsClient; // if this node is a pbft client.
std::string leaderAddrString;
std::string clientAddrString;
int32_t pbftID; 
int32_t nMaxReqInFly; 
int32_t reqWaitTimeout = 1000;
size_t maxBlockSize = 1000000; //max block size is 1MB of tx (req type, etc. excluded)
int32_t warmUpMemoryPageCache = 1;

ThreadSafeQueue::ThreadSafeQueue() { }

ThreadSafeQueue::~ThreadSafeQueue() { }

TypedReq& ThreadSafeQueue::front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    return queue_.front();
}

std::deque<TypedReq> ThreadSafeQueue::get_all() {
    std::unique_lock<std::mutex> mlock(mutex_);
    std::deque<TypedReq> ret(queue_);
    queue_.clear();
    return ret;
}

std::deque<TypedReq> ThreadSafeQueue::get_upto(size_t max_bytes) {
    std::unique_lock<std::mutex> mlock(mutex_);
    size_t packageSize = 0;
    int i = 0;
    for (; i < queue_.size() && packageSize < max_bytes; i++) {
	CMutableTransaction& tx_mutable = queue_[i].pReq->tx_mutable;
	//std::cout << "tx " << tx_mutable.GetHash().GetHex() << " size = " << ::GetSerializeSize(tx_mutable, SER_NETWORK, PROTOCOL_VERSION) << std::endl; 
	packageSize += ::GetSerializeSize(tx_mutable, SER_NETWORK, PROTOCOL_VERSION);
    }
    //std::cout << "i = " << i << ", packageSize = " << packageSize << std::endl;

    std::deque<TypedReq> ret(queue_.begin(), queue_.begin() + i);
    queue_.erase(queue_.begin(), queue_.begin() + i);
    return ret;
}

void ThreadSafeQueue::pop_front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    queue_.pop_front();
}

void ThreadSafeQueue::push_back(const TypedReq& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

void ThreadSafeQueue::push_back(TypedReq&& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(std::move(item));
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

void ThreadSafeQueue::push_back(CReqBatch& itemBatch) {
    std::unique_lock<std::mutex> mlock(mutex_);
    for (uint i = 0; i < itemBatch.vReq.size(); i++) {
        queue_.push_back(std::move(itemBatch.vReq[i]));
    }
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

CPbft::CPbft() : localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastConsecutiveSeqInReplyPhase(-1), client(nullptr), peers(std::vector<CNode*>(groupSize * num_committees)), nReqInFly(0), clientConnMan(nullptr), notEnoughReqStartTime(std::chrono::milliseconds::zero()), startBlkHeight(0), nInputShardSigs(0), lastReplySentSeq(-1),  lastBlockVerifiedThisGroup(-1), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    pubKeyMap.insert(std::make_pair(pbftID, myPubKey));
    for (int i = 0; i < 4; i++) {
	totalExeTime[i] = 0;
	totalExeCount[i] = 0;
    }
    for (int i = 0; i < STEP::NUM_STEPS; i++) {
        detailTime[i] = 0;
    }
    for (int i = 0; i < INPUT_CNT::NUM_INPUT_CNTS; i++) {
        inputCount[i] = 0;
    }
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
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */


    /* Enter prepare phase. */
    log[ppMsg.seq].phase = PbftPhase::prepare;
    /* make a pMsg */
    CPbftMessage pMsg = assembleMsg(ppMsg.seq);
    /* send the pMsg to other peers, including the leader. */
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    uint32_t start_peerID = pbftID - pbftID % groupSize;
    uint32_t end_peerID = start_peerID + groupSize;
    for (uint32_t i = start_peerID; i < end_peerID; i++) {
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
    /* In the if condition, we use >= (nFaulty << 1) withou worrying about 
     * sending more than one cMsg beacuse the log phase will turn to commit 
     * at the first time the if conditions are met. Thus the code in the if
     * block will not be executed more than once.
     * We use >= instead of == to tolerate receiving more than enough pMsg 
     * before the ppMsg arrives.
     */
    if (log[pMsg.seq].phase == PbftPhase::prepare && log[pMsg.seq].prepareCount >= (nFaulty << 1)) {
	/* Enter commit phase. */
        log[pMsg.seq].phase = PbftPhase::commit;
	/* make a cMsg */
	CPbftMessage cMsg = assembleMsg(pMsg.seq);
	/* send the cMsg to other peers */
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	uint32_t start_peerID = pbftID - pbftID % groupSize;
	uint32_t end_peerID = start_peerID + groupSize;
	for (uint32_t i = start_peerID; i < end_peerID; i++) {
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

CPbftMessage CPbft::assembleMsg(const uint32_t seq) {
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
    CReply toSent(exe_res, log[seq].ppMsg.pbft_block.vReq[idx].pReq->GetDigest());
    //uint256 hash;
    //toSent.getHash(hash);
    //privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CInputShardReply CPbft::assembleInputShardReply(const uint32_t seq, const uint32_t idx, const char exe_res, const CAmount& inputUtxoValueSum) {
    CInputShardReply toSent(exe_res, log[seq].ppMsg.pbft_block.vReq[idx].pReq->GetDigest(), inputUtxoValueSum);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

/* check if a tx of the other subgroup have create-spend dependency with tx not 
 * yet verified. 
 * There is no need to check spend-spend dependency, because the collab result 
 * will cover this type of dependency. In other words, if the tx is collab-valid,
 * it either has no spend-spend dependency with previous tx or the previous tx
 * involved is invalid. If the tx is collab-invalid, it either spends a coin never
 * exist or a previous tx has spent the coin. 
 * As a result, if the tx is collab-valid, there is no need to wait for spend-spend-conflict
 * prereq-tx to execute this tx. If the tx is collab-invalid, there is no need to
 * wait for spend-spend-conflict prereq-tx to abort the tx.
 */
bool CPbft::havePrereqTxCollab(uint32_t height, uint32_t txSeq, std::unordered_set<uint256, uint256Hasher>& preReqTxs, bool alreadyInGraph) {
    CTransaction tx = log[height].ppMsg.pbft_block.vReq[txSeq].pReq->tx_mutable;
    if (tx.IsCoinBase()) {
        return false;
    }
    for (const CTxIn& inputUtxo: tx.vin) {
        /* check create-spend dependency. */
        if (mapTxDependency.find(inputUtxo.prevout.hash) != mapTxDependency.end()) {
            preReqTxs.emplace(inputUtxo.prevout.hash);
        }
        /* check spend-spend dependency. */
        if (!alreadyInGraph) {
            /* The tx is not in the graph yet, so it is not added to the mapUtxoConflict.*/
            if (mapUtxoConflict.find(inputUtxo.prevout) != mapUtxoConflict.end()) {
                preReqTxs.insert(mapUtxoConflict[inputUtxo.prevout].begin(), mapUtxoConflict[inputUtxo.prevout].end());
                std::cout << "(" << height << ", " << txSeq << ") has " << mapUtxoConflict[inputUtxo.prevout].size() << " UTXO-conflict tx" << std::endl;
            }
        } else {
            /* The tx is already in the graph yet, be careful not to add itself as a prereq
             * tx when going through the mapUtxoConflict. */
            if (mapUtxoConflict.find(inputUtxo.prevout) == mapUtxoConflict.end() || mapUtxoConflict[inputUtxo.prevout].size() == 1) {
                /* The UTXO has been spent by other tx, 
                 * or this tx is the only tx spending this UTXO. */
                continue;
            } else {
                for (uint256& conflictTx: mapUtxoConflict[inputUtxo.prevout]) {
                    if (conflictTx != tx.GetHash()) {
                        preReqTxs.emplace(conflictTx);
                        std::cout << "UTXO-conflict tx of (" << height << ", " << txSeq << "): " << conflictTx.ToString() << std::endl;
                    }
                }
            }
        }
    }
    return preReqTxs.size() != 0;
}

void CPbft::addTx2GraphAsDependent(uint32_t height, uint32_t txSeq, std::unordered_set<uint256, uint256Hasher>& preReqTxs) {
    /* Add this tx as a dependent tx to the dependency graph. */
    for (const uint256& prereqTx: preReqTxs) {
        mapTxDependency[prereqTx].emplace_back(height, txSeq);
    }
    /* Because we only call this function when the tx is collab-valid, the  collab_status
     * field in the PendingTxStatus must be one. */
    mapPrereqCnt.emplace(TxIndexOnChain(height, txSeq), PendingTxStatus(preReqTxs.size(), 1));
}

void CPbft::addTx2GraphAsPrerequiste(CTransactionRef pTx) {
    /* add this tx as a potential prereqTx to the dependency graph. */
    mapTxDependency.emplace(pTx->GetHash(), std::deque<TxIndexOnChain>()); 
    if (pTx->IsCoinBase()) {
        return;
    }
    for (const CTxIn& inputUtxo: pTx->vin) {
        if (mapUtxoConflict.find(inputUtxo.prevout) == mapUtxoConflict.end()) {
            mapUtxoConflict.emplace(inputUtxo.prevout, std::deque<uint256>(1, pTx->GetHash())); // create an entry for this UTXO and put this tx in the list
        } else {
            /* add this tx to the UTXO spending list so that future tx spending this UTXO
             * know this tx is a prerequisite tx for it. */
            mapUtxoConflict[inputUtxo.prevout].emplace_back(pTx->GetHash());
        }
    }
}

void CPbft::informReplySendingThread(uint32_t height, std::deque<uint32_t>& qDependentTx) {
    /* inform the reply sending thread of what tx is not executed. */
    mutex4ExecutedTx.lock();
    qNotInitialExecutedTx[notExecutedQIdx].emplace_back(height, std::move(qDependentTx));
    mutex4ExecutedTx.unlock();
}

bool CPbft::executeLog() {
    /* execute all lower-seq tx until this one if possible. */
//    CCoinsViewCache view(pcoinsTip.get());
//    /* We should go on to execute all log slots that are in reply phase even
//     * their seqs are greater than the seq passed in. If we only execute up to
//     * the seq passed in, a slot missing a pbftc msg might permanently block
//     * log slots after it to be executed. */
//    for (uint i = lastExecutedSeq + 1; i < logSize && log[i].phase == PbftPhase::reply; i++) {
//	log[i].txCnt = log[i].ppMsg.pbft_block.Execute(i, g_connman.get(), view);
//	lastExecutedSeq = i;
//	std::cout << "Executed block " << i  << ", block size = " 
//		<< log[i].ppMsg.pbft_block.vReq.size()  
//		<< ", Total TX cnt = " << totalExeCount[0]
//		<< ", Total LOCK cnt = " << totalExeCount[1]
//		<< ", Total COMMIT cnt = " << totalExeCount[2]
//
//		<< std::endl;
//    }
//    bool flushed = view.Flush(); // flush to pcoinsTip
//    assert(flushed);
    return true;
}

void CPbft::executePrereqTx(const TxIndexOnChain& txIdx, std::vector<TxIndexOnChain>& validTxs, std::vector<TxIndexOnChain>& invalidTxs) {
//    std::queue<TxIndexOnChain> q;
//    std::deque<TxIndexOnChain> localQExecutedTx;
//    /* execute this prereq tx */
//    CTransactionRef pTx = log[txIdx.block_height].ppMsg.pPbftBlock->vReq[txIdx.offset_in_block];
//    ExecuteTx(*pTx, txIdx.block_height, *pcoinsTip);
//    localQExecutedTx.push_back(std::move(txIdx));
//    /* add dependent tx to the queue. */
//    for (const TxIndexOnChain& depTx: mapTxDependency[pTx->GetHash()]) {
//        if (mapPrereqCnt.find(depTx) == mapPrereqCnt.end()){
//            std::cerr << "dependent tx " << depTx.ToString() << " is not in mapPrereqCnt" << std::endl;
//            continue;
//        }
//        if(--mapPrereqCnt[depTx].remaining_prereq_tx_cnt == 0) {
//            /* prereqTx clear tx. This tx is either in our subgroup or is collab-valid.
//             * Add it to the queue for verification or execution. */
//            q.push(depTx);
//        }
//    }
//
//    /* remove this prereq tx from dependency graph. */
//    mapTxDependency.erase(pTx->GetHash());
//    /* remove all input UTXO of this tx from mapUtxoConflict. Because mapPrereqCnt 
//     * is only for spend-spend conflict detection instead of tracking (which is done
//     * by the mapTxDependency), there is no need to change the mapPrereqCnt when 
//     * cleaning mapUtxoConflict. */
//    if (!pTx->IsCoinBase()) {
//        for (const CTxIn& inputUtxo: pTx->vin) {
//            std::unordered_map<COutPoint, std::deque<uint256>, OutpointHasher>::iterator iter = mapUtxoConflict.find(inputUtxo.prevout);
//            //if (iter == mapUtxoConflict.end()) {
//            //    std::cout << "input UTXO " << inputUtxo.ToString() << " of tx " << pTx->GetHash().ToString() << " does not exist in utxo conflict map. txIdx =  " << txIdx.ToString() << std::endl;
//            //}
//            assert(iter != mapUtxoConflict.end()); 
//            /* remove the whole entry b/c future tx spending this UTXO can be verified
//             * without waitinf for any tx spending this UTXO (as this UTXO has already
//             * been spent, the future tx must be invalid). 
//             * On the other hand, when cleaning mapUtxoConflict after a tx is dealt with
//             * as invalid, we can only remove the tx in the entry b/c the validity of a
//             * future tx spending the UTXO stills depends on the validity of other tx
//             * in the entry.
//             */
//            mapUtxoConflict.erase(iter); // remove the entry for this UTXO 
//        }
//    }
//
//    /* verify the dependent tx belonging to our group. */
//    while (!q.empty()) {
//        const TxIndexOnChain& curTxIdx = q.front();
//        q.pop();
//        pTx = log[curTxIdx.block_height].ppMsg.pPbftBlock->vReq[curTxIdx.offset_in_block];
//        switch (mapPrereqCnt[curTxIdx].collab_status) {
//            case 2:
//            {
//                /* this is a tx in our subgroup, verify it. */
//                bool isValid = VerifyTx(*pTx, curTxIdx.block_height, *pcoinsTip);
//                if (isValid) {
//                    validTxs.push_back(curTxIdx);
//                    localQExecutedTx.push_back(curTxIdx);
//                    /* remove input UTXOs from mapUtxoConflict */
//                    for (const CTxIn& inputUtxo: pTx->vin) {
//                        std::unordered_map<COutPoint, std::deque<uint256>, OutpointHasher>::iterator iter = mapUtxoConflict.find(inputUtxo.prevout); 
//                        assert(iter != mapUtxoConflict.end()); 
//                        mapUtxoConflict.erase(iter); // remove the entry for this UTXO 
//                    }
//                } else {
//                    invalidTxs.push_back(curTxIdx);
//                    /* remove the tx from mapUtxoConflict */
//                    for (const CTxIn& inputUtxo: pTx->vin) {
//                        std::unordered_map<COutPoint, std::deque<uint256>, OutpointHasher>::iterator iter = mapUtxoConflict.find(inputUtxo.prevout);
//                        assert(iter != mapUtxoConflict.end()); 
//                        if (iter->second.size() == 1) {
//                            /* this tx is the only tx spending this UTXO, remove 
//                             * the whole entry. */
//                            mapUtxoConflict.erase(iter); // remove the entry for this UTXO 
//                        } else {
//                            /* there are other tx spending the UTXO, remove only
//                             * the tx in this entry.*/
//                            std::deque<uint256>::iterator deqIter = std::find(iter->second.begin(), iter->second.end(), pTx->GetHash());
//                            iter->second.erase(deqIter);  
//                        }
//                    }
//                }
//                break;
//            }
//            case 1:
//                /* this is a collab-valid tx, execute it. */
//                ExecuteTx(*pTx, curTxIdx.block_height, *pcoinsTip);
//                /* remove input UTXOs from mapUtxoConflict */
//                for (const CTxIn& inputUtxo: pTx->vin) {
//                    std::unordered_map<COutPoint, std::deque<uint256>, OutpointHasher>::iterator iter = mapUtxoConflict.find(inputUtxo.prevout); 
//                    assert(iter != mapUtxoConflict.end()); 
//                    mapUtxoConflict.erase(iter); // remove the entry for this UTXO 
//                }
//                localQExecutedTx.push_back(curTxIdx);
//                break;
//            default:
//                std::cerr << "invalid collab_status = " << mapPrereqCnt[curTxIdx].collab_status << std::endl;
//        }
//
//        /* add dependent tx to the queue. */
//        for (const TxIndexOnChain& depTx: mapTxDependency[pTx->GetHash()]) {
//            if (mapPrereqCnt.find(depTx) == mapPrereqCnt.end()){
//                std::cerr << "dependent tx " << depTx.ToString() << " is not in mapPrereqCnt" << std::endl;
//                continue;
//            }
//            if(--mapPrereqCnt[depTx].remaining_prereq_tx_cnt == 0) {
//                /* prereqTx clear tx. This tx is either in our subgroup or is collab-valid.
//                 * Add it to the queue for verification or execution. */
//                q.push(depTx);
//            }
//        }
//        /* remove the tx from dependency graph */
//        mapTxDependency.erase(pTx->GetHash());
//        mapPrereqCnt.erase(curTxIdx);
//    }
//
//    nCompletedTx += localQExecutedTx.size();
//    /* inform the reply sending thread of what tx has been executed. */
//    mutex4ExecutedTx.lock();
//    qExecutedTx[executedQIdx].insert(qExecutedTx[executedQIdx].end(), localQExecutedTx.begin(), localQExecutedTx.end());
//    mutex4ExecutedTx.unlock();
}


void CPbft::saveBlocks2File() const {
    FILE* file = fsbridge::fopen("pbft_blocks_tx_placement.out", "wb+");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to write." << std::endl;
        return;
    }
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);

    fileout.write((char*)&lastConsecutiveSeqInReplyPhase, sizeof(lastConsecutiveSeqInReplyPhase));
    for (int i = 0; i <= lastConsecutiveSeqInReplyPhase; i++) {
        log[i].ppMsg.pbft_block.Serialize(fileout);
    }
}

int CPbft::readBlocksFromFile() {
    FILE* file = fsbridge::fopen("pbft_blocks_tx_placement.out", "rb");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to read." << std::endl;
        return 0;
    }
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);

    int lastExecutedSeqWarmUp = 0;
    filein.read((char*)&lastExecutedSeqWarmUp, sizeof(lastExecutedSeqWarmUp));
    std::cout << __func__ << ": lastExecutedSeqWarmUp = " << lastExecutedSeqWarmUp << std::endl;
    for (int i = 0; i < lastExecutedSeqWarmUp + 1; i++) {
        try {
            log[i].ppMsg.pbft_block.Unserialize(filein);
        }
        catch (const std::exception& e) {
            std::cerr << "Deserialize or I/O error when reading PBFT block " << i << ": " << e.what() << std::endl;
        }
    }
    return lastExecutedSeqWarmUp;
}

void CPbft::WarmUpMemoryCache() {
    CCoinsViewCache view_tenta(pcoinsTip.get());
    int lastExecutedSeqWarmUp = readBlocksFromFile();
    uint32_t nWarmUpTx = 0;
    for (int i = 0; i <= lastExecutedSeqWarmUp; i++) {
	uint32_t block_size = log[i].ppMsg.pbft_block.vReq.size();
	//std::cout << "executing warm-up block of size " << block_size << std::endl;

        log[i].ppMsg.pbft_block.WarmUpExecute(i, view_tenta);
        /* Discard the block to prepare for performance test. */
        log[i].ppMsg.pbft_block.Clear();
	nWarmUpTx += block_size; 
    }
    std::cout << "warm up -- total executed tx: " << nWarmUpTx << std::endl;
}


void ThreadConsensusLogExe() {
    RenameThread("log-exe");
    while (!ShutdownRequested()) {
        bool busy = g_pbft->executeLog();
        if (!busy) {
            MilliSleep(10);
        }
    }
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

        const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
        const uint256& block_hash = log[toSent.height].ppMsg.pbft_block.hash;
        for (uint32_t i = 0; i < groupSize; i++) {
            if (!isInVerifySubGroup(i, block_hash)) {
                /* this is a peer in the other subgroup for this block. */
                //std::cout << "sending collab msg for block " << toSent.height << " to peer " << i << std::endl;
                mapBlockOtherSubgroup[toSent.height].push_back(i);
                g_connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::COLLAB_VRF, toSent));
            }
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
        for (auto peerId: mapBlockOtherSubgroup[tx.block_height]) {
            otherSubgroupSendQ[peerId].validTxs.push_back(tx);
        }
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
        //std::cout << "sending multiBlkCollabMsg to peer " << i << ". valid tx cnt = " << toSent.validTxs.size() << ", invalid tx cnt = " << toSent.invalidTxs.size() << std::endl;
        uint256 hash;
        toSent.getHash(hash);
        privateKey.Sign(hash, toSent.vchSig);
        toSent.sigSize = toSent.vchSig.size();
        g_connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::COLLAB_MULTI_BLK, toSent));
        toSent.clear();
    }

    return true;
}

BlockCollabRes::BlockCollabRes(): collab_msg_full_tx_cnt(0) { }

BlockCollabRes::BlockCollabRes(uint32_t txCnt): tx_collab_valid_cnt(txCnt), collab_msg_full_tx_cnt(0) { }

void CPbft::UpdateTxValidity(const CCollabMessage& msg) {
    //std::cout << "received collab msg for block " << msg.height << " from peer " << msg.peerID << std::endl;
    if (!checkCollabMsg(msg)) {
        std::cerr << "collab msg invalid" << std::endl;
        return;
    }

    uint32_t lowestHeightOutstandingBlock = firstOutstandingBlock;
    if (msg.height < lowestHeightOutstandingBlock) {
        return;
    }

    std::deque<TxIndexOnChain> localValidTxQ;
    std::deque<TxIndexOnChain> localInvalidTxQ;
    if (mapBlockCollabRes.find(msg.height) == mapBlockCollabRes.end()) {
        if (msg.height > lastConsecutiveSeqInReplyPhase) {
            log[msg.height].blockSizeInCollabMsg = msg.txCnt;
        }
        else {
            log[msg.height].blockSizeInCollabMsg = log[msg.height].ppMsg.pbft_block.vReq.size();
        }
        mapBlockCollabRes.emplace(msg.height, BlockCollabRes(log[msg.height].blockSizeInCollabMsg));
        //std::cout << "create collab msg counters for block "<< msg.height << ", tx count = " << log[msg.height].ppMsg.pPbftBlock->vReq.size() << std::endl;
    }
    
    BlockCollabRes& block_collab_res = mapBlockCollabRes[msg.height];

    if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
        /* all tx in this block has accumlated enough collab_verify res. ignore this msg.*/
        return;
    }


    uint32_t validTxCntInMsg = 0;
    for (uint i = 0; i < block_collab_res.tx_collab_valid_cnt.size(); i++) {
        if (block_collab_res.tx_collab_valid_cnt[i] == nFaulty + 1) {
            /* this tx has collect enough collab_valid msg. */
            continue;
        }
        if (msg.isValidTx(i)) {
            validTxCntInMsg++;
            block_collab_res.tx_collab_valid_cnt[i]++;
            if (block_collab_res.tx_collab_valid_cnt[i] == nFaulty + 1) {
                block_collab_res.collab_msg_full_tx_cnt++;
                /* add the tx to validTxQ */
                localValidTxQ.emplace_back(msg.height, i);
            }

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

    //std::cout << "processed collab msg for block " << msg.height << " from peer " << msg.peerID << ". valid tx cnt = " << validTxCntInMsg << ", invalid tx cnt = " << msg.invalidTxs.size() << std::endl;

    /* prune entries in mapBlockCollabRes.
     * All blocks meeting the following two conditions can be removed:
     * 1) have height lower than < lastBlockVerifiedThisGroup 
     * 2) whose tx are all fully collab-verified 
     */
    for (auto iter = mapBlockCollabRes.begin(); iter != mapBlockCollabRes.end() && iter->first < lowestHeightOutstandingBlock;) {
        mapBlockCollabRes.erase(iter++);
    }
    
    /* add tx in local queues to the global queues*/
    mutex4Q.lock();
    qValidTx[1 - validTxQIdx].insert(qValidTx[1 - validTxQIdx].end(), localValidTxQ.begin(),  localValidTxQ.end());
    qInvalidTx[1 - invalidTxQIdx].insert(qInvalidTx[1 - invalidTxQIdx].end(), localInvalidTxQ.begin(), localInvalidTxQ.end());
    mutex4Q.unlock();

}

void CPbft::UpdateTxValidity(const CCollabMultiBlockMsg& msg) {
    //std::cout << "received collabMulBlk msg from peer " << msg.peerID << std::endl;
    if (!checkCollabMulBlkMsg(msg)) 
        return;

    std::deque<TxIndexOnChain> localValidTxQ;
    std::deque<TxIndexOnChain> localInvalidTxQ;
    /* add valid tx to the BlockCollabRes map*/
    uint32_t lowestHeightOutstandingBlock = firstOutstandingBlock;
    for (TxIndexOnChain txIdx: msg.validTxs) {
        if (txIdx.block_height < lowestHeightOutstandingBlock) {
            /* This block is no longer outstanding.
             * All tx in this block have been executed, either using collab res or
             * we verified them by ourselves due to timeout waiting for collab res,
             * so we can safely discard this message.
             */
            continue;
        }


        /* because CollabMulBlkMsg contains only info for previous blockly verified tx,
         * the entry for the block must exist in  mapBlockCollabRes. */
        BlockCollabRes& block_collab_res = mapBlockCollabRes[txIdx.block_height];

        if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
            /* all tx in this block has accumlated enough collab_verify res. ignore this tx.
             * This block is not pruned yet because we prune blocks consequtively.
             */
            continue;
        }

        if (block_collab_res.tx_collab_valid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            /* this tx has collect enough collab_valid msg. */
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
        if (txIdx.block_height < lowestHeightOutstandingBlock) {
            /* This block is no longer outstanding.
             * All tx in this block have been executed, either using collab res or
             * we verified them by ourselves due to timeout waiting for collab res,
             * so we can safely discard this message.
             */
            continue;
        }

        BlockCollabRes& block_collab_res = mapBlockCollabRes[txIdx.block_height];
        block_collab_res.map_collab_invalid_cnt[txIdx.offset_in_block]++;
        if (block_collab_res.map_collab_invalid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to inValidTxQ */
            localInvalidTxQ.push_back(std::move(txIdx));
        }
    }

    /* prune entries in mapBlockCollabRes.
     * All blocks meeting the following two conditions can be removed:
     * 1) have height lower than < lastBlockVerifiedThisGroup 
     * 2) whose tx are all fully collab-verified 
     */
    for (auto iter = mapBlockCollabRes.begin(); iter != mapBlockCollabRes.end() && iter->first < lowestHeightOutstandingBlock;) {
        mapBlockCollabRes.erase(iter++);
    }

    //std::cout << "processed collabMulBlk msg from peer " << msg.peerID << ", has " << msg.validTxs.size() << " valid tx, " <<  msg.invalidTxs.size() << " invalid tx" << std::endl;

    /* add tx in local queues to the global queues*/
    mutex4Q.lock();
    qValidTx[1 - validTxQIdx].insert(qValidTx[1 - validTxQIdx].end(), localValidTxQ.begin(),  localValidTxQ.end());
    qInvalidTx[1 - invalidTxQIdx].insert(qInvalidTx[1 - invalidTxQIdx].end(), localInvalidTxQ.begin(), localInvalidTxQ.end());
    mutex4Q.unlock();

}

void CPbft::sendReplies(CConnman* connman) {
}

PendingTxStatus::PendingTxStatus(): remaining_prereq_tx_cnt(0), collab_status(0) { }
PendingTxStatus::PendingTxStatus(uint32_t remaining_prereq_tx_cnt_in, char collab_status_in): remaining_prereq_tx_cnt(remaining_prereq_tx_cnt_in), collab_status(collab_status_in) { }

InitialBlockExecutionStatus::InitialBlockExecutionStatus(){ };
InitialBlockExecutionStatus::InitialBlockExecutionStatus(uint32_t heightIn, std::deque<uint32_t>&& dependentTxsIn): height(heightIn), dependentTxs(dependentTxsIn){ };

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

CSkippedBlockEntry::CSkippedBlockEntry(const struct timeval& blockMetTimeIn, std::deque<char>&& collabStatusIn, uint32_t outstandingTxCntIn):blockMetTime(blockMetTimeIn), collabStatus(collabStatusIn), outstandingTxCnt(outstandingTxCntIn) { }

std::unique_ptr<CPbft> g_pbft;
/* In case we receive an omniledger unlock_to_abort req, store a copy of all locked coins
 * so that they can be added back to pcoinsTip. */
std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> lockedCoinMap;
