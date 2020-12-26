/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <pbft/pbft.h>
#include "tx_placement/tx_placer.h"
#include "netmessagemaker.h"

int32_t pbftID;
struct timeval thruInterval; // calculate throughput once every "thruInterval" seconds

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

void ThreadSafeTxIndexSet::lock_free_insert(const TxIndexOnChain& txIdx) {
    set_.insert(txIdx);
} 

void ThreadSafeTxIndexSet::erase(const TxIndexOnChain& txIdx) {
    std::unique_lock<std::mutex> mlock(mutex_);
    set_.erase(txIdx);
}

bool ThreadSafeTxIndexSet::haveTx(const TxIndexOnChain& txIdx) {
    std::unique_lock<std::mutex> mlock(mutex_);
    if (set_.find(txIdx) != set_.end()) 
        return true;
    else
        return false;
}

size_t ThreadSafeTxIndexSet::size() {
    std::unique_lock<std::mutex> mlock(mutex_);
    const size_t size = set_.size();
    mlock.unlock();
    return size;
}

bool ThreadSafeTxIndexSet::empty() {
    std::unique_lock<std::mutex> mlock(mutex_);
    return set_.empty();
}

ThreadSafeVector::ThreadSafeVector(uint32_t size, double initial_val): vector_(size, initial_val) { }

void ThreadSafeVector::add(uint32_t index, double value) {
    std::unique_lock<std::mutex> mlock(mutex_);
    vector_[index] += value;
}

void ThreadSafeVector::print() {
    std::unique_lock<std::mutex> mlock(mutex_);
    for (uint32_t i = 0; i < vector_.size(); i++)
        std::cout << " shard " << i << " = " << vector_[i];
}

const float CPbft::LOAD_TX = 1.0f;
const float CPbft::LOAD_LOCK = 3.85f;
const float CPbft::LOAD_COMMIT = 4.82f;

CPbft::CPbft() : leaders(std::vector<CNode*>(num_committees)), nLastCompletedTx(0), nCompletedTx(0), nTotalFailedTx(0), nSucceed(0), nFail(0), nCommitted(0), nAborted(0), vLoad(num_committees, 0), batchBuffers(num_committees), privateKey(CKey()) {
    testStartTime = {0, 0};
    nextLogTime = {0, 0};
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    latencySingleShardFile.open("/home/l27ren/tx_placement/latencySingleShard.out");
    latencyCrossShardFile.open("/home/l27ren/tx_placement/latencyCrossShard.out");
    thruputFile.open("/home/l27ren/tx_placement/thruput.out");
}

CPbft::~CPbft() {
    latencySingleShardFile.close();
    latencyCrossShardFile.close();
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
        thruputFile << endTime.tv_sec << "." << endTime.tv_usec << ", sending tx starts.\n";
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
    thruputFile << endTime.tv_sec << "." << endTime.tv_usec << "," << nCompletedTxCopy << "," << thruput << std::endl;
}

void CPbft::loadDependencyGraph (){
    std::ifstream dependencyFileStream;
    dependencyFileStream.open(getDependencyFilename());
    assert(!dependencyFileStream.fail());
    DependencyRecord dpRec;
    dpRec.Unserialize(dependencyFileStream);
    while (!dependencyFileStream.eof()) {
	mapDependency[dpRec.tx].push_back(dpRec.prereq_tx);
        uncommittedPrereqTxSet.lock_free_insert(dpRec.prereq_tx);
	dpRec.Unserialize(dependencyFileStream);
    }
    dependencyFileStream.close();
}

void CPbft::add2Batch(const uint32_t shardId, const ClientReqType type, const CTransactionRef txRef) {
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	const uint256& hashTx = txRef->GetHash();
	struct TxStat stat;
    std::shared_ptr<CClientReq> req;
    if (type == ClientReqType::TX) { 
    stat.type = TxType::SINGLE_SHARD;
        req = std::make_shared<TxReq>(*txRef);
    }
    else {
    stat.type = TxType::CROSS_SHARD;
        req = std::make_shared<LockReq>(*txRef);
    }
    req->UpdateHash();
    batchBuffers[shardId].emplace_back(type, req);
    mapTxStartTime.insert(std::make_pair(hashTx, stat));

    /* if batch for the shard is full, send the batch. */
    if (batchBuffers[shardId].isFull()) {
        for (uint i = 0; i < batchBuffers[shardId].vReq.size(); i++) { 
            const uint256& hashTx = batchBuffers[shardId].vReq[i].pReq->hash;
            /* all req in the batch have the same start time. */
            gettimeofday(&(mapTxStartTime[hashTx].startTime), NULL);
        }
        g_connman->PushMessage(leaders[shardId], msgMaker.Make(NetMsgType::REQ_BATCH, batchBuffers[shardId]));
        batchBuffers[shardId].vReq.clear();
    }
}

/* no matter any batch is full or not, send all batches. */
void CPbft::sendAllBatch() {
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    for (uint shardId = 0; shardId < num_committees; shardId++) {
        for (uint i = 0; i < batchBuffers[shardId].vReq.size(); i++) { 
            const uint256& hashTx = batchBuffers[shardId].vReq[i].pReq->hash;
            /* all req in the batch have the same start time. */
            gettimeofday(&(mapTxStartTime[hashTx].startTime), NULL);
        }
        g_connman->PushMessage(leaders[shardId], msgMaker.Make(NetMsgType::REQ_BATCH, batchBuffers[shardId]));
        batchBuffers[shardId].vReq.clear();
    }
}

std::unique_ptr<CPbft> g_pbft;
