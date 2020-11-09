/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <pbft/pbft.h>
#include "tx_placement/tx_placer.h"
int32_t pbftID;
struct timeval thruInterval; // calculate throughput once every "thruInterval" 

TxBlockInfo::TxBlockInfo(): blockHeight(0), n(0) { }
TxBlockInfo::TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, TxIndexOnChain latest_prereq_tx_in): tx(txIn), blockHeight(blockHeightIn), n(nIn), latest_prereq_tx(latest_prereq_tx_in) { }

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

void SentTxHeap::push(const std::vector<TxIndexOnChain>& localSentTx) {
    std::unique_lock<std::mutex> mlock(mutex_);
    for (const TxIndexOnChain& txIdx: localSentTx) {
	pq_.push(txIdx);
    }
    std::cout << "after inserted, pq_ size = " << pq_.size() << std::endl;
    updateGreatestConsecutive();
}

size_t SentTxHeap::updateGreatestConsecutive(){
    TxIndexOnChain LCCTx = g_pbft->latestConsecutiveSentTx.load(std::memory_order_relaxed);
    int i = 1;
    while (!pq_.empty() && pq_.top() == LCCTx + i) {
	pq_.pop();
	i++;
    }
    LCCTx = LCCTx + (i - 1);
    g_pbft->latestConsecutiveSentTx.store(LCCTx, std::memory_order_relaxed);
    std::cout << "latest consecutive commited tx = " << LCCTx.ToString() << std::endl;
    return pq_.size();
}

size_t SentTxHeap::size() {
    std::unique_lock<std::mutex> mlock(mutex_);
    size_t size = pq_.size();
    mlock.unlock();
    return size;
}

bool SentTxHeap::empty() {
    std::unique_lock<std::mutex> mlock(mutex_);
    return pq_.empty();
}

CPbft::CPbft() : leaders(std::vector<CNode*>(num_committees)), latestConsecutiveSentTx(TxIndexOnChain(600999, 2932)), nLastCompletedTx(0), nCompletedTx(0), nTotalFailedTx(0), nTotalSentTx(0), nonTailCnt(0), tailStartTime({0, 0}), privateKey(CKey()) {
    testStartTime = {0, 0};
    nextLogTime = {0, 0};
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    latencyFile.open ("/home/l27ren/collab_verify/latency.out");
    thruputFile.open ("/home/l27ren/collab_verify/thruput.out");
}

CPbft::~CPbft() {
    latencyFile.close();
    thruputFile.close();
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
	thruputFile << endTime.tv_sec << "." << endTime.tv_usec << ", sending tx starts.\n" ;
	testStartTime = endTime;
	/* log when thruInterval has passed by  */
	nextLogTime = endTime + thruInterval;
	return;
    }
    uint32_t nCompletedTxCopy = nCompletedTx.load(std::memory_order_relaxed); 
    struct timeval timeElapsed = endTime - (nextLogTime - thruInterval);
    double thruput = (nCompletedTxCopy - nLastCompletedTx) / (timeElapsed.tv_sec + timeElapsed.tv_usec * 0.000001);

    nextLogTime = endTime + thruInterval;
    nLastCompletedTx = nCompletedTxCopy;
    thruputFile << endTime.tv_sec << "." << endTime.tv_usec << "," <<  nCompletedTxCopy << "," << thruput << std::endl;
}

bool operator<(const TxBlockInfo& a, const TxBlockInfo& b)
{
    return a.latest_prereq_tx < b.latest_prereq_tx;
}

bool operator>(const TxBlockInfo& a, const TxBlockInfo& b)
{
    return a.latest_prereq_tx > b.latest_prereq_tx;
}


std::unique_ptr<CPbft> g_pbft;
