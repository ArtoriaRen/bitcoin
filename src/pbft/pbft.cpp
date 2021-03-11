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

CPbft::CPbft(): localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastConsecutiveSeqInReplyPhase(-1), client(nullptr), peers(std::vector<CNode*>(groupSize)), nReqInFly(0), nCompletedTx(0), clientConnMan(nullptr), lastQSizePrintTime(std::chrono::milliseconds::zero()), totalVerifyTime(0), totalVerifyCnt(0), totalExeTime(0), lastBlockVerifiedThisGroup(-1), lastBlockValidSentSeq(-1), lastReplySentSeq(-1), lastCollabFullBlock(-1), qValidTx(2), qInvalidTx(2), validTxQIdx(0), invalidTxQIdx(0), otherSubgroupSendQ(groupSize), notEnoughReqStartTime(std::chrono::milliseconds::zero()), privateKey(CKey()) {
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


void CPbft::executeLog(struct timeval& start_process_first_block) {
    /* 1. Verify and execute prereq-clear tx in sequece and in the granularty 
     * of blocks for blocks belonging to our subgroup.
     * 2. Execute COLLAB_VERIFIED tx. If some tx belonging to our subgroup 
     * becomes prerequisite, verifiy and execute them.
     * 3. If there is nothing to be verified in Steps 1 or 2, verify the first 
     * not yet verified block belonging to the other subgroup (in the granularity
     * of tx). And remove them from the dependency graph.  
     */
    struct timeval start_time, end_time;
    /* Step 1: verify the successor block of the last block we have verified. if it belongs to our group. */
    if (lastBlockVerifiedThisGroup < lastConsecutiveSeqInReplyPhase){
        uint32_t curHeight = lastBlockVerifiedThisGroup + 1;
        CPbftBlock& block = *log[curHeight].ppMsg.pPbftBlock;
        if(isInVerifySubGroup(pbftID, block.hash)) {
            /* This is a block to be verified by our subgroup. 
             * Verify and Execute prerequiste-clear tx in this block.
             * (The VerifyTx call includes executing tx.) 
             */
            gettimeofday(&start_time, NULL);
            std::vector<char> validTxs(block.vReq.size() >> 3);
            std::vector<uint32_t> invalidTxs;
            std::cout << "verifying block " << curHeight << " of size " << block.vReq.size() << std::endl;
            uint32_t validTxCnt = block.Verify(curHeight, *pcoinsTip, validTxs, invalidTxs);
            gettimeofday(&end_time, NULL);
            lastBlockVerifiedThisGroup++;
            nCompletedTx += validTxCnt;
            /*TODO: Anounce the verifying result to peers in the other subgroup.*/
            SendCollabMsg(curHeight, validTxs, invalidTxs);
            std::cout << "Average verify time of block " << curHeight << ": " << ((end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec)) / validTxCnt << " us/req" << ". valid tx cnt = " << validTxCnt << ". invalid tx cnt = " << invalidTxs.size() << std::endl;
            logServerSideThruput(start_process_first_block, end_time, curHeight);
        } else {
            std::cout << "add " << block.vReq.size() << " tx in block " << curHeight << " to dependency graph." << std::endl;
            /* tx belongs to the other group are not added to mapPreqCnt b/c we are not interest in its prereq tx until later we have to verify such tx by ourselves. */
            for (CTransactionRef tx: block.vReq) {
                mapTxDependency.emplace(tx->GetHash(), std::list<TxIndexOnChain>()); 
            }
            /*Although we did not verified tx in this block, we advance the pointer so that we would not check the same block next time. */
            lastBlockVerifiedThisGroup++;
        }
    }

    /* TODO: Step 2: Execute COLLAB_VERIFIED tx. */
    /* for each COLLAB message, check if the tx are in the dependency graph.
     * If yes, executed it provided valid, othewise check the next tx. */
    if (!qValidEmpty.load(std::memory_order_relaxed)) {
        std::vector<TxIndexOnChain> validTxs;
        std::vector<TxIndexOnChain> invalidTxs;
        for (const TxIndexOnChain& txIdx: qValidTx[validTxQIdx]) {
            CTransactionRef pTx = log[txIdx.block_height].ppMsg.pPbftBlock->vReq[txIdx.offset_in_block];
            executePrereqTx(txIdx, validTxs, invalidTxs);
        }
        SendCollabMultiBlkMsg(validTxs, invalidTxs);
        qValidTx[validTxQIdx].clear();
        qValidEmpty.store(true, std::memory_order_relaxed);
    }
    /* TODO: for tx in qInValidTx, remove them without executing. 
     * Update the prereq cnt of its dependent tx. 
     * Verify a dependent tx if it is prereq-clear and belongs to our group.
     */
    

    /* TODO: Step 3 (Optional): If there is nothing to be verified in Steps 1 or 2, verify the first 
     * not yet verified block belonging to the other subgroup . */
}

void CPbft::executePrereqTx(const TxIndexOnChain& txIdx, std::vector<TxIndexOnChain>& validTxs, std::vector<TxIndexOnChain>& invalidTxs) {
    std::queue<TxIndexOnChain> q;
    /* execute this prereq tx */
    CTransactionRef pTx = log[txIdx.block_height].ppMsg.pPbftBlock->vReq[txIdx.offset_in_block];
    ExecuteTx(*pTx, txIdx.block_height, *pcoinsTip);
    nCompletedTx++;
    /* add dependent tx to the queue. */
    for (const TxIndexOnChain& depTx: mapTxDependency[pTx->GetHash()]) {
        if (mapPrereqCnt.find(depTx) == mapPrereqCnt.end()){
            /* this dependent tx has been executed due to collab vrf.*/
            continue;
        }
        if(--mapPrereqCnt[depTx] == 0 && isInVerifySubGroup(pbftID, log[depTx.block_height].ppMsg.pPbftBlock->hash)) {
            /* prereqTx clear and in our group. Add it to the queue for verification. */
            q.push(depTx);
        }
    }
    /* remove this prereq tx from dependency graph. */
    mapTxDependency.erase(pTx->GetHash());

    /* verify the dependent tx belonging to our group. */
    while (!q.empty()) {
        const TxIndexOnChain& curTxIdx = q.front();
        q.pop();
        pTx = log[curTxIdx.block_height].ppMsg.pPbftBlock->vReq[curTxIdx.offset_in_block];
        bool isValid = VerifyTx(*pTx, curTxIdx.block_height, *pcoinsTip);
        if (isValid) {
            validTxs.push_back(curTxIdx);
            nCompletedTx++;
        } else {
            invalidTxs.push_back(curTxIdx);
        }

        /* add dependent tx to the queue. */
        for (const TxIndexOnChain& depTx: mapTxDependency[pTx->GetHash()]) {
            if (mapPrereqCnt.find(depTx) == mapPrereqCnt.end()){
                /* this dependent tx has been executed due to collab vrf.*/
                continue;
            }
            if(--mapPrereqCnt[depTx] == 0 && isInVerifySubGroup(pbftID, log[depTx.block_height].ppMsg.pPbftBlock->hash)) {
                /* prereqTx clear. */
                q.push(depTx);
            }
        }
        /* remove the tx from dependency graph */
        mapTxDependency.erase(pTx->GetHash());
        std::map<TxIndexOnChain, uint32_t>::iterator it = mapPrereqCnt.find(curTxIdx);
        assert(it != mapPrereqCnt.end());
        mapPrereqCnt.erase(it);
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

bool CPbft::SendCollabMsg(uint32_t height, std::vector<char>& validTxs, std::vector<uint32_t>& invalidTxs) {
    CCollabMessage toSent(height, std::move(validTxs), std::move(invalidTxs)); 
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();

    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    const uint256& block_hash = log[height].ppMsg.pPbftBlock->hash;
    for (uint32_t i = 0; i < groupSize; i++) {
        if (!isInVerifySubGroup(i, block_hash)) {
            /* this is a peer in the other subgroup for this block. */
            std::cout << "sending collab msg for block " << height << " to peer " << i << std::endl;
            mapBlockOtherSubgroup[height].push_back(i);
            g_connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::COLLAB_VRF, toSent));
        }
    } 
    return true;
}

bool CPbft::SendCollabMultiBlkMsg(const std::vector<TxIndexOnChain>& validTxs, const std::vector<TxIndexOnChain>& invalidTxs) {
    for (const TxIndexOnChain& tx: validTxs) {
        for (auto peerId: mapBlockOtherSubgroup[tx.block_height]) {
            otherSubgroupSendQ[peerId].validTxs.push_back(tx);
        }
    }
    for (const TxIndexOnChain& tx: invalidTxs) {
        for (auto peerId: mapBlockOtherSubgroup[tx.block_height]) {
            otherSubgroupSendQ[peerId].invalidTxs.push_back(tx);
        }
    }

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

BlockCollabRes::BlockCollabRes(): collab_msg_full_tx_cnt(0) { }

BlockCollabRes::BlockCollabRes(uint32_t txCnt): tx_collab_valid_cnt(txCnt), collab_msg_full_tx_cnt(0) { }

void CPbft::UpdateTxValidity(const CCollabMessage& msg) {
    std::cout << "received collab msg for block " << msg.height << " from peer " << msg.peerID << std::endl;
    if (!checkCollabMsg(msg)) {
        std::cerr << "collab msg invalid" << std::endl;
        return;
    }
    
    if (mapBlockCollabRes.find(msg.height) == mapBlockCollabRes.end()) {
        mapBlockCollabRes.emplace(msg.height, BlockCollabRes(log[msg.height].ppMsg.pPbftBlock->vReq.size()));
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
                qValidTx[1 - validTxQIdx].emplace_back(msg.height, i);
            }

        }
    }

    for (const auto txSeq: msg.invalidTxs) {
        block_collab_res.map_collab_invalid_cnt[txSeq]++;
        if (block_collab_res.map_collab_invalid_cnt[txSeq] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to inValidTxQ */
            qInvalidTx[1 - invalidTxQIdx].emplace_back(msg.height, txSeq);
        }
    }

    std::cout << "processed collab msg for block " << msg.height << " from peer " << msg.peerID << ". valid tx cnt = " << validTxCntInMsg << ", invalid tx cnt = " << msg.invalidTxs.size() << std::endl;

    /* prune entries in mapBlockCollabRes */
    if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
        for(; lastCollabFullBlock <= lastConsecutiveSeqInReplyPhase 
                && mapBlockCollabRes[lastCollabFullBlock + 1].collab_msg_full_tx_cnt == mapBlockCollabRes[lastCollabFullBlock + 1].tx_collab_valid_cnt.size(); 
                lastCollabFullBlock++) {
            mapBlockCollabRes.erase(lastCollabFullBlock);
        }
    }
    
    /* check if the pointers of queue pairs should be switched. */
    if (qValidEmpty.load(std::memory_order_relaxed) && !qValidTx[1 - validTxQIdx].empty()) {
        validTxQIdx = 1 - validTxQIdx; 
        qValidEmpty.store(false, std::memory_order_relaxed);
    }
    if (qInValidEmpty.load(std::memory_order_relaxed) && !qInvalidTx[1 - invalidTxQIdx].empty()) {
        invalidTxQIdx = 1 - invalidTxQIdx; 
        qInValidEmpty.store(false, std::memory_order_relaxed);
    }
}

void CPbft::UpdateTxValidity(const CCollabMultiBlockMsg& msg) {
    std::cout << "processing collabMulBlk msg for block from peer " << msg.peerID << std::endl;
    if (!checkCollabMulBlkMsg(msg)) 
        return;

    /* add valid tx to the BlockCollabRes map*/
    for (TxIndexOnChain txIdx: msg.validTxs) {
        /* because CollabMulBlkMsg contains only info for previous blockly verified tx,
         * the entry for the block must exist in  mapBlockCollabRes. */
        BlockCollabRes& block_collab_res = mapBlockCollabRes[txIdx.block_height];

        if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
            /* all tx in this block has accumlated enough collab_verify res. ignore this msg.
             * This block is not pruned yet because we prune blocks consequtively.
             */
            return;
        }

        if (block_collab_res.tx_collab_valid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            /* this tx has collect enough collab_valid msg. */
            continue;
        }
        block_collab_res.tx_collab_valid_cnt[txIdx.offset_in_block]++;
        if (block_collab_res.tx_collab_valid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to validTxQ */
            qValidTx[1 - validTxQIdx].push_back(std::move(txIdx));
        }
        /* prune entries in mapBlockCollabRes */
        if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
            for(; lastCollabFullBlock <= lastConsecutiveSeqInReplyPhase 
                    && mapBlockCollabRes[lastCollabFullBlock + 1].collab_msg_full_tx_cnt == mapBlockCollabRes[lastCollabFullBlock + 1].tx_collab_valid_cnt.size(); 
                    lastCollabFullBlock++) {
                mapBlockCollabRes.erase(lastCollabFullBlock);
            }
        }
    }

    /* add invalid tx to the BlockCollabRes map*/
    for (TxIndexOnChain txIdx: msg.invalidTxs) {
        BlockCollabRes& block_collab_res = mapBlockCollabRes[txIdx.block_height];
        block_collab_res.map_collab_invalid_cnt[txIdx.offset_in_block]++;
        if (block_collab_res.map_collab_invalid_cnt[txIdx.offset_in_block] == nFaulty + 1) {
            block_collab_res.collab_msg_full_tx_cnt++;
            /* add the tx to inValidTxQ */
            qInvalidTx[1 - invalidTxQIdx].push_back(std::move(txIdx));
        }
        /* prune entries in mapBlockCollabRes */
        if (block_collab_res.collab_msg_full_tx_cnt == block_collab_res.tx_collab_valid_cnt.size()) {
            for(; lastCollabFullBlock <= lastConsecutiveSeqInReplyPhase 
                    && mapBlockCollabRes[lastCollabFullBlock + 1].collab_msg_full_tx_cnt == mapBlockCollabRes[lastCollabFullBlock + 1].tx_collab_valid_cnt.size(); 
                    lastCollabFullBlock++) {
                mapBlockCollabRes.erase(lastCollabFullBlock);
            }
        }
    }

    
    /* check if the pointers of queue pairs should be switched. */
    if (qValidEmpty.load(std::memory_order_relaxed) && !qValidTx[1 - validTxQIdx].empty()) {
        validTxQIdx = 1 - validTxQIdx; 
        qValidEmpty.store(false, std::memory_order_relaxed);
    }
    if (qInValidEmpty.load(std::memory_order_relaxed) && !qInvalidTx[1 - invalidTxQIdx].empty()) {
        invalidTxQIdx = 1 - invalidTxQIdx; 
        qInValidEmpty.store(false, std::memory_order_relaxed);
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
    /*TODO: may not need warm up anymore if using SSD, but still, they are slower
     * than memory. The goal of warm up is to load UTXOs to be spent into memory,
     * and this match the practical use case. */
//    CCoinsViewCache view_tenta(pcoinsTip.get());
//    readBlocksFromFile(nWarmUpBlocks);
//    for (int i = 0; i < nWarmUpBlocks; i++) {
//        log[i].ppMsg.pPbftBlock->Verify(i, view_tenta);
//        /* Discard the block to prepare for performance test. */
//        log[i].ppMsg.pPbftBlock.reset();
//    }
}

void ThreadConsensusLogExe() {
    RenameThread("bitcoin-logexe");
    struct timeval start_process_first_block = {0, 0};
    while (!ShutdownRequested()) {
        g_pbft->executeLog(start_process_first_block);
        MilliSleep(50);
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

std::unique_ptr<CPbft> g_pbft;
