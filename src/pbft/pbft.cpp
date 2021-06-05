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

TxStat::TxStat(): outputShard(-1) { }
TxStat::TxStat(CTransactionRef txIn, int32_t outputShardIn): tx(txIn),outputShard(outputShardIn) { }

CPbft::CPbft() : leaders(num_committees), pubKeys(num_committees * groupSize), nLastCompletedTx(0), nCompletedTx(0), nTotalFailedTx(0), nSucceed(0), nFail(0), nCommitted(0), nAborted(0), batchBuffers(num_committees), vBatchBufferMutex(num_committees), expected_tx_latency(num_committees), vecShardTxCount(num_committees, 0), loadScores(num_committees, 0), nSingleShard(0), nCrossShard(0), privateKey(CKey()) {
    testStartTime = {0, 0};
    nextLogTime = {0, 0};
    nextShardLoadPrintTime = {0, 0};
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    latencySingleShardFile.open("/hdd2/davina/tx_placement/latencySingleShard.out");
    latencyCrossShardFile.open("/hdd2/davina/tx_placement/latencyCrossShard.out");
    thruputFile.open("/hdd2/davina/tx_placement/thruput.out");
    shardLoadFile.open("/hdd2/davina/tx_placement/shardLoads.out");
    recordedSentTx.open("/hdd2/davina/tx_placement/recordedSentTx.out");
}

CPbft::~CPbft() {
    latencySingleShardFile.close();
    latencyCrossShardFile.close();
    thruputFile.close();
    shardLoadFile.close();
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
        thruputFile << endTime.tv_sec << "." << endTime.tv_usec << ",0,0 \n";
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

void CPbft::logShardLoads(struct timeval& endTime) {
    uint shardLogInterval = 10; // uint is second
    if (testStartTime.tv_sec == 0) {
	/* test just started. log the start time */
    shardLoadFile << "time";
    for (uint i = 0; i < num_committees; i++) {
        shardLoadFile << ",shard"<< i;
    }
    shardLoadFile << ",nSingleShard,nCrossShard\n";
    shardLoadFile << endTime.tv_sec << "." << endTime.tv_usec;
    for (uint i = 0; i < num_committees + 2; i++) {
        shardLoadFile << ",0";
    }
    shardLoadFile << "\n";
	/* log every shardLogInterval seconds. */
	nextShardLoadPrintTime.tv_sec = endTime.tv_sec + shardLogInterval;
	nextShardLoadPrintTime.tv_usec = endTime.tv_usec;
	return;
    }
    shardLoadFile << endTime.tv_sec << "." << endTime.tv_usec;
    for (uint i = 0; i < num_committees; i++) {
        shardLoadFile << "," << vecShardTxCount[i];
    }
    shardLoadFile << "," << nSingleShard << "," << nCrossShard << "\n";
    /* log every shardLogInterval seconds. */
    nextShardLoadPrintTime.tv_sec = endTime.tv_sec + shardLogInterval;
    nextShardLoadPrintTime.tv_usec = endTime.tv_usec;
}

void CPbft::add2Batch(const uint32_t shardId, const ClientReqType type, const CTransactionRef txRef, std::deque<std::shared_ptr<CClientReq>>& threadLocalBatchBuffer, const std::vector<uint32_t>* utxoIdxToLock) {
    const uint256& hashTx = txRef->GetHash();
    std::shared_ptr<CClientReq> req;
    if (type == ClientReqType::TX) { 
        req = std::make_shared<TxReq>(txRef);
    }
    else {
        req = std::make_shared<LockReq>(txRef, *utxoIdxToLock);
    }
    if (vBatchBufferMutex[shardId].try_lock()) {
        uint batchBufferSize = batchBuffers[shardId].vPReq.size();
        if (batchBufferSize < MAX_BATCH_SIZE) {
            if ((MAX_BATCH_SIZE - batchBufferSize) >= threadLocalBatchBuffer.size()) {
                /* we can add all thread locally buffered item to the global buffer,
                 * and then add this req. 
                 */
                batchBuffers[shardId].vPReq.insert(batchBuffers[shardId].vPReq.end(), threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.end());
                batchBuffers[shardId].vPReq.push_back(req);
                vBatchBufferMutex[shardId].unlock();
                threadLocalBatchBuffer.clear();
            } else {
                uint appendSize = MAX_BATCH_SIZE - batchBufferSize;
                batchBuffers[shardId].vPReq.insert(batchBuffers[shardId].vPReq.end(), threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.begin() + appendSize);
                vBatchBufferMutex[shardId].unlock();
                threadLocalBatchBuffer.erase(threadLocalBatchBuffer.begin(), threadLocalBatchBuffer.begin() + appendSize);
                threadLocalBatchBuffer.push_back(req);
            }
        } else {
            vBatchBufferMutex[shardId].unlock();
            threadLocalBatchBuffer.push_back(req);
            //std::cout << __func__ << " add to thread local buffer tx = " << req->pTx->GetHash().ToString() << std::endl;
        }
    } else {
        threadLocalBatchBuffer.push_back(req);
        //std::cout << __func__ << " add to thread local buffer tx = " << req->pTx->GetHash().ToString() << std::endl;
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
                    gettimeofday(&(pbft.mapTxStat[hashTx].startTime), NULL);
                    //std::cout << __func__ << ": sending tx " << hashTx.ToString() << " to shard " << shardId << std::endl;
                }
                struct timeval start, end;
                totalPushMessageCnt += pbft.batchBuffers[shardId].vPReq.size();
                //gettimeofday(&start, NULL);
                g_connman->PushMessage(pbft.leaders[shardId], msgMaker.Make(NetMsgType::REQ_BATCH, pbft.batchBuffers[shardId]));
                pbft.batchBuffers[shardId].vPReq.clear();
                pbft.vBatchBufferMutex[shardId].unlock();
                //gettimeofday(&end, NULL);
                //totalPushMessageTime += (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
            }
        }
        if (bufferEmpty) {
            MilliSleep(1);
        }
        fShutdown = ShutdownRequested();
    }
    //std::cout << "Batch-sending thread: average push message time = " << totalPushMessageTime/totalPushMessageCnt << " usec/tx. totally pushed msg cnt =  " << totalPushMessageCnt << std::endl;
}

void CPbft::loadBlocks(uint32_t startBlock, uint32_t endBlock) {
    blocks2Send.resize(endBlock - startBlock);
    for (int block_height = startBlock; block_height < endBlock; block_height++) {
        if (ShutdownRequested())
            break;
        CBlockIndex* pblockindex = chainActive[block_height];
        if (!ReadBlockFromDisk(blocks2Send[block_height - startBlock], pblockindex, Params().GetConsensus())) {
            std::cerr << "Block not found on disk" << std::endl;
        }
    }
}

CShardLatency::CShardLatency(): latency(0){ }

void CPbft::probeShardLatency() {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    for (uint i = 0; i < num_committees; i++) {
        gettimeofday(&(expected_tx_latency[i].probe_send_time), NULL);
        g_connman->PushMessage(leaders[i], msgMaker.Make(NetMsgType::LATENCY_PROBE));
        //std::cout << "send probe to shard " << i << std::endl;
    }
}

void CPbft::updateLoadScore(uint shard_id, ClientReqType reqType, uint nSigs) {
    switch(reqType) {
        case ClientReqType::TX:
            loadScores[shard_id] += 150 * nSigs + 35;
            break;
        case ClientReqType::LOCK:
            loadScores[shard_id] += 170 * nSigs + 192;
            break;
        case ClientReqType::UNLOCK_TO_COMMIT:
            loadScores[shard_id] += 125 * nSigs + 5;
            break;
        default:
            std::cout << __func__ << "invalid client req type " << std::endl;
    }
}

std::unique_ptr<CPbft> g_pbft;
