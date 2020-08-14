/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <pbft/pbft.h>
#include "tx_placement/tx_placer.h"
int32_t pbftID;
uint32_t thruInterval; // calculate throughput once completing every "thruInterval" tx

TxBlockInfo::TxBlockInfo(): blockHeight(0), n(0), aborted(false) { }
TxBlockInfo::TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, uint32_t nInputShardsIn): tx(txIn), blockHeight(blockHeightIn), n(nIn), nInputShards(nInputShardsIn) { }

ThreadSafeQueue::ThreadSafeQueue() { }

ThreadSafeQueue::~ThreadSafeQueue() { }

TxBlockInfo& ThreadSafeQueue::front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    return queue_.front();
}

void ThreadSafeQueue::pop_front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    queue_.pop_front();
}

void ThreadSafeQueue::push_back(const TxBlockInfo& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread

}

void ThreadSafeQueue::push_back(TxBlockInfo&& item) {
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

CReplyBlockStat::CReplyBlockStat(): nConfirm(0) {
// TODO: set the reply block to be null so that we can distinguish if a replyBlock has been received.
}


//CPbft::CPbft() : leaders(std::vector<CNode*>(num_committees)), nCompletedTx(0), nCommitNoResendTx(0), nAbortedTx(0), privateKey(CKey()) {
CPbft::CPbft() : leaders(std::vector<CNode*>(num_committees)), lastCompletedTx(0), nCompletedTx(0), privateKey(CKey()) {
    thruStartTime = {0, 0};
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
}

bool CPbft::checkReplySig(const CReply* pReply) const {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(pReply->peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << pReply->peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    pReply->getHash(msgHash);
    if (!it->second.Verify(msgHash, pReply->vchSig)) {
        std::cerr << "verification sig fail for sender " << pReply->peerID << std::endl;
        return false;
    }
    return true;
}

bool CPbft::checkReplyBlockSig(CReplyBlock* pReplyBlock) const {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(pReplyBlock->peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << pReplyBlock->peerID << std::endl;
        return false;
    }
    pReplyBlock->UpdateMerkleRoot();
    if (!it->second.Verify(pReplyBlock->hashMerkleRoot, pReplyBlock->vchSig)) {
        std::cerr << "verification sig fail for sender " << pReplyBlock->peerID << std::endl;
        return false;
    }
    return true;
}

void CPbft::logThruput(struct timeval& endTime) {
    uint32_t thruput = 0;
    if (thruStartTime.tv_sec != 0) {
	thruput = (nCompletedTx - lastCompletedTx) * 1000000 / ((endTime.tv_sec - thruStartTime.tv_sec) * 1000000 + (endTime.tv_usec - thruStartTime.tv_usec));
    }
    lastCompletedTx = nCompletedTx; 
    thruStartTime = endTime;
    std::cout << "At time " << endTime.tv_sec << "." << endTime.tv_usec << ", completed " <<  nCompletedTx << "tx" << ": throughput = " << thruput << ". ";
    std::cout << g_pbft->nCommitNoResendTx << " tx committed with the first sending, " << g_pbft->nAbortedTx << " tx aborted with the first sending. " << std::endl;
}


std::unique_ptr<CPbft> g_pbft;
