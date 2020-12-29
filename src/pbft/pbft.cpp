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

TxBlockInfo::TxBlockInfo(): blockHeight(0), n(0), outputShard(-1) { }
TxBlockInfo::TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, int32_t outputShardIn): tx(txIn), blockHeight(blockHeightIn), n(nIn), outputShard(outputShardIn) { }

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

uint32_t ThreadSafeVector::minEleIndex() {
    std::unique_lock<std::mutex> mlock(mutex_);
    return std::min_element(vector_.begin(), vector_.end()) - vector_.begin();
}

void ThreadSafeVector::print() {
    std::unique_lock<std::mutex> mlock(mutex_);
    for (uint32_t i = 0; i < vector_.size(); i++)
        std::cout << " shard " << i << " = " << vector_[i];
}

const float CPbft::LOAD_TX = 1.0f;
const float CPbft::LOAD_LOCK = 3.85f;
const float CPbft::LOAD_COMMIT = 4.82f;

CPbft::CPbft() : leaders(std::vector<CNode*>(num_committees)), nLastCompletedTx(0), nCompletedTx(0), nTotalFailedTx(0), nSucceed(0), nFail(0), nCommitted(0), nAborted(0), vLoad(num_committees, 0), batchBuffers(num_committees), vBatchBufferMutex(num_committees), privateKey(CKey()) {
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

void CPbft::loadDependencyGraph(uint32_t startBlock, uint32_t endBlock) {
    std::ifstream dependencyFileStream;
    dependencyFileStream.open(getDependencyFilename());
    assert(!dependencyFileStream.fail());
    DependencyRecord dpRec;
    dpRec.Unserialize(dependencyFileStream);
    while (!dependencyFileStream.eof()) {
        if (dpRec.tx.block_height < endBlock) {
            mapRemainingPrereq[dpRec.tx]++;
            mapDependentTx[dpRec.prereq_tx].push_back(dpRec.tx);
        }
        dpRec.Unserialize(dependencyFileStream);
    }
    dependencyFileStream.close();
}

void CPbft::add2Batch(const uint32_t shardId, const ClientReqType type, const CTransactionRef txRef, std::deque<TypedReq>& threadLocalBatchBuffer, const std::vector<uint32_t>* utxoIdxToLock) {
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
        req = std::make_shared<LockReq>(*txRef, *utxoIdxToLock);
    }
    req->UpdateHash(); 
    mapTxStartTime.insert(std::make_pair(hashTx, stat));
    if (vBatchBufferMutex[shardId].try_lock()) {
        batchBuffers[shardId].emplace_back(type, req);
        batchBuffers[shardId].vReq.insert(batchBuffers[shardId].vReq.end(), threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.end());
        vBatchBufferMutex[shardId].unlock();
    } else {
        threadLocalBatchBuffer.emplace_back(type, req);
    }
}

static long totalPushMessageTime = 0;
static uint totalPushMessageCnt = 0;
/* no matter any batch is full or not, send all batches. */
void sendAllBatch() {
    bool fShutdown = ShutdownRequested();
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    bool pushedSomething = false;
    CPbft& pbft = *g_pbft;
    while (!fShutdown && !(sendingDone && totalPushMessageCnt == totalTxSent))
    {
        pushedSomething = false;
        for (uint shardId = 0; shardId < num_committees; shardId++) {
            if (pbft.batchBuffers[shardId].vReq.empty())
                continue;
            for (uint i = 0; i < pbft.batchBuffers[shardId].vReq.size(); i++) { 
                const uint256& hashTx =  pbft.batchBuffers[shardId].vReq[i].pReq->hash;
                /* all req in the batch have the same start time. */
                gettimeofday(&(pbft.mapTxStartTime[hashTx].startTime), NULL);
            }
            struct timeval start, end;
            totalPushMessageCnt += pbft.batchBuffers[shardId].vReq.size();
            gettimeofday(&start, NULL);
            g_connman->PushMessage(pbft.leaders[shardId], msgMaker.Make(NetMsgType::REQ_BATCH, pbft.batchBuffers[shardId]));
            gettimeofday(&end, NULL);
            totalPushMessageTime += (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
            pushedSomething = true;
            std::cout << "send a batch of " << pbft.batchBuffers[shardId].vReq.size() << " req takes " << (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec)  << " us, average push message time = " << totalPushMessageTime/totalPushMessageCnt << " usec/tx " << std::endl;
            pbft.batchBuffers[shardId].vReq.clear();
        }
        if (!pushedSomething) {
            MilliSleep(200);
        }
        fShutdown = ShutdownRequested();
    }
}

void CPbft::loadShardInfo(const int txStartBlock, const int txEndBlock) {
    allBlockShardInfo.resize(txEndBlock - txStartBlock);
    for (int block_height = txStartBlock; block_height < txEndBlock; block_height++) {
        uint32_t block_size = chainActive[block_height]->nTx;
        std::cout << __func__ << ": loading shard info for " << block_size << " tx in block " << block_height << std::endl;
        std::ifstream shardInfoFile;
        shardInfoFile.open(getShardInfoFilename(block_height));
        assert(shardInfoFile.is_open());
        /* we did not clear allBlockShardInfo b/c it will be overwirtten during file unserialization. */
        allBlockShardInfo[block_height - txStartBlock].resize(block_size);
        for (uint i = 0; i < allBlockShardInfo.size(); i++) {
            allBlockShardInfo[block_height - txStartBlock][i].Unserialize(shardInfoFile);
        }
        shardInfoFile.close();
    }
}

template<typename T>
static std::string vector_to_string(const std::vector<T>& vec) {
    std::string ret;
    for (uint i = 0; i < vec.size(); i++) {
        ret += std::to_string(vec[i]);
        ret += ',';
    }
    return ret.substr(0, ret.size() - 1);
}

void ShardInfo::print() const {
    std::cout <<  vector_to_string(shards) << std::endl;
    for(uint i = 0; i < vShardUtxoIdxToLock.size(); i++) {
	std::cout << "shard " << shards[i+1] << " should lock UTXO idx: ";
	std::cout << vector_to_string(vShardUtxoIdxToLock[i]) << std::endl; 
    }
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

void CPbft::BuildIndepTxQueue(uint32_t startBlock, uint32_t endBlock) {
    indepTx2Send.resize(endBlock - startBlock);
    for (uint block_height = startBlock; block_height < endBlock; block_height++) {
        for (uint i = 0; i < blocks2Send[block_height - startBlock].vtx.size(); i++) {
            if (mapRemainingPrereq.find(TxIndexOnChain(block_height, i)) == mapRemainingPrereq.end()) {
                /* this tx has no prereq tx*/
                indepTx2Send[block_height - startBlock].push_back(i);
            }
        }
    }
}

std::unique_ptr<CPbft> g_pbft;
