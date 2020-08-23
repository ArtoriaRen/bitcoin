/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <pbft/pbft.h>
#include "tx_placement/tx_placer.h"
int32_t pbftID;
uint32_t thruInterval; // calculate throughput once every "thruInterval" seconds

TxBlockInfo::TxBlockInfo(): blockHeight(0), n(0) { }
TxBlockInfo::TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn): tx(txIn), blockHeight(blockHeightIn), n(nIn) { }

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

CPbft::CPbft() : leaders(std::vector<CNode*>(num_committees)), nLastCompletedTx(0), nCompletedTx(0), nTotalFailedTx(0), nTotalSentTx(0), privateKey(CKey()) {
    testStartTime = {0, 0};
    nextLogTime = {0, 0};
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

void CPbft::logThruput(struct timeval& endTime) {
    if (testStartTime.tv_sec == 0) {
	/* test just started. log the start time */
	std::cout << "At time " << endTime.tv_sec << "." << endTime.tv_usec << ", sending tx starts. This is the initial throughput log. " << std::endl;
	testStartTime = endTime;
	/* log when thruInterval seconds has passed by  */
	nextLogTime.tv_sec = endTime.tv_sec + thruInterval;
	nextLogTime.tv_usec = endTime.tv_usec;
	return;
    }
    double thruput = (nCompletedTx - nLastCompletedTx) / ((endTime.tv_sec - nextLogTime.tv_sec + thruInterval) + (endTime.tv_usec - nextLogTime.tv_usec) * 0.000001);
    /* log when thruInterval seconds has passed by  */
    nextLogTime.tv_sec = endTime.tv_sec + thruInterval;
    nextLogTime.tv_usec = endTime.tv_usec;
    nLastCompletedTx = nCompletedTx;
    std::cout << "At time " << endTime.tv_sec << "." << endTime.tv_usec << ", completed " <<  nCompletedTx << " tx" << ": throughput = " << thruput << std::endl;
}


std::unique_ptr<CPbft> g_pbft;
