/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <pbft/pbft.h>
#include "tx_placement/tx_placer.h"
#include "netmessagemaker.h"
#include "chainparams.h"
#include <init.h>

int32_t pbftID;
struct timeval thruInterval; // calculate throughput once every "thruInterval" seconds

TxBlockInfo::TxBlockInfo(): blockHeight(0), n(0), outputShard(-1), childTxns(0) { }
TxBlockInfo::TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, int32_t outputShardIn): tx(txIn), blockHeight(blockHeightIn), n(nIn), outputShard(outputShardIn), childTxns(0) { }

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

CPbft::CPbft() : leaders(num_committees), pubKeys(num_committees * groupSize), nLastCompletedTx(0), nCompletedTx(0), nTotalFailedTx(0), nSucceed(0), nFail(0), nCommitted(0), nAborted(0), vLoad(num_committees, 0), batchBuffers(num_committees), vBatchBufferMutex(num_committees), privateKey(CKey()) {
    testStartTime = {0, 0};
    nextLogTime = {0, 0};
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    latencySingleShardFile.open("/hdd2/davina/tx_placement/latencySingleShard.out");
    latencyCrossShardFile.open("/hdd2/davina/tx_placement/latencyCrossShard.out");
    thruputFile.open("/hdd2/davina/tx_placement/thruput.out");
    recordedSentTx.open("/hdd2/davina/tx_placement/recordedSentTx.out");
}

CPbft::~CPbft() {
    latencySingleShardFile.close();
    latencyCrossShardFile.close();
    thruputFile.close();
    recordedSentTx.close();
}

bool CPbft::checkReplySig(const CReply* pReply) const {
    // verify signature and return wrong if sig is wrong
    CPubKey pubKey = pubKeys[pReply->peerID];
    if (!pubKey.IsValid()) {
        std::cout << "no pub key for peer" << pReply->peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    pReply->getHash(msgHash);
    if (!pubKey.Verify(msgHash, pReply->vchSig)) {
        std::cout << "verification sig fail for peer" << pReply->peerID << std::endl;
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

void CPbft::add2Batch(const uint32_t shardId, const ClientReqType type, const CTransactionRef txRef, std::deque<std::shared_ptr<CClientReq>>& threadLocalBatchBuffer, const std::vector<uint32_t>* utxoIdxToLock) {
    const uint256& hashTx = txRef->GetHash();
    struct TxStat stat;
    std::shared_ptr<CClientReq> req;
    if (type == ClientReqType::TX) { 
        stat.type = TxType::SINGLE_SHARD;
        req = std::make_shared<TxReq>(txRef);
    }
    else {
        stat.type = TxType::CROSS_SHARD;
        req = std::make_shared<LockReq>(txRef, *utxoIdxToLock);
    }
    mapTxStartTime.insert(std::make_pair(hashTx, stat));
    if (vBatchBufferMutex[shardId].try_lock()) {
        uint batchBufferSize = batchBuffers[shardId].vPReq.size();
        if (batchBufferSize < MAX_BATCH_SIZE) {
            uint32_t appendSize = (MAX_BATCH_SIZE - batchBufferSize) > threadLocalBatchBuffer.size() ? threadLocalBatchBuffer.size() : (MAX_BATCH_SIZE - batchBufferSize);
            batchBuffers[shardId].vPReq.insert(batchBuffers[shardId].vPReq.end(), threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.begin() + appendSize);
            batchBuffers[shardId].vPReq.push_back(req);
            vBatchBufferMutex[shardId].unlock();
            threadLocalBatchBuffer.erase(threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.begin() + appendSize);
        } else {
            vBatchBufferMutex[shardId].unlock();
            threadLocalBatchBuffer.push_back(req);
        }
    } else {
        threadLocalBatchBuffer.push_back(req);
    }
}

void CPbft::add2BatchOnlyBuffered(const uint32_t shardId, std::deque<std::shared_ptr<CClientReq>>& threadLocalBatchBuffer) {
    if (vBatchBufferMutex[shardId].try_lock()) {
        uint batchBufferSize = batchBuffers[shardId].vPReq.size();
        if (batchBufferSize < MAX_BATCH_SIZE) {
            uint32_t appendSize = (MAX_BATCH_SIZE - batchBufferSize) > threadLocalBatchBuffer.size() ? threadLocalBatchBuffer.size() : (MAX_BATCH_SIZE - batchBufferSize);
            batchBuffers[shardId].vPReq.insert(batchBuffers[shardId].vPReq.end(), threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.begin() + appendSize);
            vBatchBufferMutex[shardId].unlock();
            threadLocalBatchBuffer.erase(threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.begin() + appendSize);
        } else {
            vBatchBufferMutex[shardId].unlock();
        }
    }
}

/* no matter any batch is full or not, send all batches. */
void sendAllBatch() {
    RenameThread("batchSending");
    long totalPushMessageTime = 0;
    uint totalPushMessageCnt = 0;
    bool fShutdown = ShutdownRequested();
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    CPbft& pbft = *g_pbft;
    bool bufferEmpty = false;
    while (!fShutdown && !(sendingDone && totalPushMessageCnt == globalReqSentCnt))
    {
        bufferEmpty = true;
        for (uint shardId = 0; shardId < num_committees; shardId++) {
            if (pbft.vBatchBufferMutex[shardId].try_lock()) {
                if (pbft.batchBuffers[shardId].vPReq.empty()) {
                    pbft.vBatchBufferMutex[shardId].unlock();
                    continue;
                }
                bufferEmpty = false;
                for (uint i = 0; i < pbft.batchBuffers[shardId].vPReq.size(); i++) { 
                    const uint256& hashTx =  pbft.batchBuffers[shardId].vPReq[i]->pTx->GetHash();
                    /* all req in the batch have the same start time. */
                    gettimeofday(&(pbft.mapTxStartTime[hashTx].startTime), NULL);
                    //std::cout << __func__ << ": sending tx " << hashTx.ToString() << std::endl;
                }
                struct timeval start, end;
                totalPushMessageCnt += pbft.batchBuffers[shardId].vPReq.size();
                gettimeofday(&start, NULL);
                g_connman->PushMessage(pbft.leaders[shardId], msgMaker.Make(NetMsgType::REQ_BATCH, pbft.batchBuffers[shardId]));
                pbft.batchBuffers[shardId].vPReq.clear();
                pbft.vBatchBufferMutex[shardId].unlock();
                gettimeofday(&end, NULL);
                totalPushMessageTime += (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
            }
        }
        if (bufferEmpty) {
            MilliSleep(1);
        }
        fShutdown = ShutdownRequested();
    }
    std::cout << "Batch-sending thread: average push message time = " << totalPushMessageTime/totalPushMessageCnt << " usec/tx. totally pushed msg cnt =  " << totalPushMessageCnt << std::endl;
}

void CPbft::loadBlocks(uint32_t startBlock, uint32_t endBlock) {
    blocks2Send.resize(endBlock - startBlock);
    for (int block_height = startBlock; block_height < endBlock; block_height++) {
        CBlockIndex* pblockindex = chainActive[block_height];
        if (!ReadBlockFromDisk(blocks2Send[block_height - startBlock], pblockindex, Params().GetConsensus())) {
            std::cerr << "Block not found on disk" << std::endl;
        }
    }
}

std::unique_ptr<CPbft> g_pbft;
