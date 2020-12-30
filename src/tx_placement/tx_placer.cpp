/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <tx_placement/tx_placer.h>

#include "hash.h"
#include "arith_uint256.h"
#include "chain.h"
#include "validation.h"
#include "netmessagemaker.h"
#include <thread>
#include <chrono>
#include <time.h>
#include "txdb.h"
#include "init.h"

std::atomic<uint32_t> totalTxSent(0);
bool sendingDone = false;
static const uint32_t SEC = 1000000; // 1 sec = 10^6 microsecond

uint32_t num_committees;
int lastAssignedAffinity = -1;
/* txChunkSize * numThreads should be less than block size. Otherwise, some thread
 * has nothing to do. We should consertively set the num of threads less than 20 
 * given the txChunkSize of 100.
 */
size_t txChunkSize = 100; 
//uint32_t txStartBlock;
//uint32_t txEndBlock;

TxPlacer::TxPlacer():totalTxNum(0){}


/* all output UTXOs of a tx is stored in one shard. */
std::vector<int32_t> TxPlacer::randomPlace(const CTransaction& tx, const CCoinsViewCache& cache){
    std::vector<int32_t> ret;

    std::set<int> inputShardIds;
    /* add the input shard ids to the set */
    if (!tx.IsCoinBase()) { // do not calculate shard for dummy coinbase input.
	for(uint32_t i = 0; i < tx.vin.size(); i++) {
	    inputShardIds.insert(tx.vin[i].prevout.hash.GetCheapHash() % num_committees);
	}
    }

//    std::cout << "tx " << tx->GetHash().GetHex() << " spans shards : ";
//    for(auto entry : shardIds) {
//	std::cout << entry << " ";
//    }
//    std::cout << std::endl;

    /* add the output shard id to the above set */
    int32_t outShardId = tx.GetHash().GetCheapHash() % num_committees;
    if (inputShardIds.find(outShardId) != inputShardIds.end()) {
	/* inputShardIds.size() is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size()]++;
    } else {
	/* inputShardIds.size() + 1 is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size() + 1]++;
    }

    ret.resize(inputShardIds.size() + 1);
    ret[0] = outShardId;// put the outShardIt as the first element
    std::copy(inputShardIds.begin(), inputShardIds.end(), ret.begin() + 1);
    return ret;
}

int32_t TxPlacer::randomPlaceUTXO(const uint256& txid) {
    return txid.GetCheapHash() % num_committees;
}


void TxPlacer::printPlaceResult(){
    std::cout << "total tx num = " << totalTxNum << std::endl;
    std::cout << "tx shard num stats : " << std::endl;
    for(auto entry: shardCntMap) {
	std::cout << "\t" <<  entry.first << "-input_UTXO tx: " << std::endl;
	for (auto p : entry.second) {
	    std::cout << "\t\t" << p.first << "-shard tx count = " << p.second << std::endl;
	}
    }
} 

static void delayByNoop(const int noop_count) {
    int k = 0;
    uint oprand = noop_count;

    for (; k < noop_count; k++) {
	if (ShutdownRequested())
	    return;
	oprand ^= k;
    }
    std::cerr << "loop noop for " << k << " times. oprand becomes " << oprand << std::endl;
}

static uint32_t sendTxChunk(const CBlock& block, const uint start_height, const uint block_height, const uint32_t start_tx, const int noop_count, std::vector<std::deque<TypedReq>>& batchBuffers, uint32_t& reqSentCnt) {
    uint32_t cnt = 0;
    CPbft& pbft = *g_pbft;
    uint32_t end_tx = std::min(start_tx + txChunkSize, pbft.indepTx2Send[block_height - start_height] .size());

    for (uint j = start_tx; j < end_tx; j++) {
        uint32_t tx_offset = pbft.indepTx2Send[block_height - start_height][j];
        sendTx(block.vtx[tx_offset], tx_offset, block_height, batchBuffers, reqSentCnt);
        cnt++;
        //txIdx.Serialize(g_pbft->recordedSentTx);
        /* delay by doing noop. */
        //delayByNoop(noop_count);
    }
    return cnt;
}

static uint32_t sendQueuedTx(const int startBlock, const int noop_count, std::vector<std::deque<TypedReq>>& batchBuffers, uint32_t& reqSentCnt) {
    int txSentCnt = 0;
    CPbft& pbft = *g_pbft;
    if (pbft.depTxReady2Send.empty())
        return txSentCnt; 
    
    /* only send all tx in the queue if we hold the lock. */
    if (pbft.depTxMutex.try_lock()) {
        if (!pbft.depTxReady2Send.empty()) {
            //txSentCnt += pbft.depTxReady2Send.size();
            for (const TxIndexOnChain& txIdx: pbft.depTxReady2Send) {
                //std::cout << "found queued tx. addr =  " << &txIdx << ", tx = " << txIdx.ToString() << std::endl;
                sendTx(pbft.blocks2Send[txIdx.block_height - startBlock].vtx[txIdx.offset_in_block], txIdx.offset_in_block, txIdx.block_height, batchBuffers, reqSentCnt);
                txSentCnt++;
            }
std::cout << "depTxReady2Send size = " << pbft.depTxReady2Send.size() << ", tx sent cnt = " << txSentCnt << std::endl;
            pbft.depTxReady2Send.clear();
        }
        pbft.depTxMutex.unlock();
    }
    
    //std::cout << __func__ << " sent " <<  txSentCnt << " queued tx. queue size = "  << listDelaySendingTx.size() << std::endl;
    return txSentCnt;
}


void sendTxOfThread(const int startBlock, const int endBlock, const uint32_t thread_idx, const uint32_t num_threads, const int noop_count) {
    RenameThread(("sendTx" + std::to_string(thread_idx)).c_str());
    uint32_t cnt = 0, reqSentCnt = 0;
    const uint32_t jump_length = num_threads * txChunkSize;
    std::vector<std::deque<TypedReq>> batchBuffers(num_committees);
    TxPlacer txPlacer;
    CPbft& pbft = *g_pbft;
    struct timeval start_time, end_time;
    struct timeval start_time_all_block, end_time_all_block;
    gettimeofday(&start_time_all_block, NULL);
    for (int block_height = startBlock; block_height < endBlock; block_height++) {
        if (ShutdownRequested())
            break;
        CBlock& block = g_pbft->blocks2Send[block_height - startBlock];
        for (size_t i = thread_idx * txChunkSize; i < pbft.indepTx2Send[block_height - startBlock] .size(); i += jump_length){
            //std::cout << __func__ << ": thread " << thread_idx << " sending No." << i << " tx in block " << block_height << std::endl;
            gettimeofday(&start_time, NULL);
            uint32_t actual_chunk_size = sendTxChunk(block, startBlock, block_height, i, noop_count, batchBuffers, reqSentCnt);
            gettimeofday(&end_time, NULL);
            cnt += actual_chunk_size;
            //std::cout << __func__ << ": thread " << thread_idx << " sent " << actual_chunk_size << " tx in block " << block_height << ". The sending takes " << (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_usec - start_time.tv_usec) << " us." << std::endl;

            /* check delay sending tx after sending a chunk.  */
            gettimeofday(&start_time, NULL);
            uint32_t cnt_queued_tx_sent = sendQueuedTx(startBlock, noop_count, batchBuffers, reqSentCnt);
            cnt += cnt_queued_tx_sent;
            gettimeofday(&end_time, NULL);
            //std::cout << "sent " <<  cnt_queued_tx_sent << " tx queued. The sending takes " << (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_usec - start_time.tv_usec) << " us." << std::endl;
        }
    }

    /* add all req in our local batch buffer to the global batch buffer. */
    bool localBuffersEmpty = false;
    while (!localBuffersEmpty) {
        localBuffersEmpty = true;
        for (uint i = 0; i < batchBuffers.size(); i++) {
            std::deque<TypedReq>& shardBatchBuffer = batchBuffers[i];
            if (!shardBatchBuffer.empty()) {
                localBuffersEmpty = false;
                pbft.add2BatchOnlyBuffered(i, shardBatchBuffer);
            }
        }
    }

    gettimeofday(&end_time_all_block, NULL);
    std::cout << __func__ << ": thread " << thread_idx << " sent " << cnt << " tx in total. All tx of this thread takes " << (end_time_all_block.tv_sec - start_time_all_block.tv_sec)*1000000 + (end_time_all_block.tv_usec - start_time_all_block.tv_usec) << " us. Totally sentReqCnt = " << reqSentCnt << std::endl;
    totalTxSent += cnt; 
}

//void sendRecordedTxOfThread(const int startBlock, const int endBlock, const uint32_t thread_idx, const uint32_t num_threads, const int noop_count) {
//    RenameThread(("sendTx" + std::to_string(thread_idx)).c_str());
//    uint32_t cnt = 0;
//    std::ifstream recordedSentTxFile;
//    recordedSentTxFile.open("/hdd2/davina/tx_placement/recordedSentTxForRead.out");
//    assert(!recordedSentTxFile.fail());
//    TxPlacer txPlacer;
//    for (int block_height = startBlock; block_height < endBlock; block_height++) {
//        if (ShutdownRequested())
//            break;
//
//        CBlock block;
//        CBlockIndex* pblockindex = chainActive[block_height];
//        if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
//            std::cerr << "Block not found on disk" << std::endl;
//        }
//        std::cout << " block_height = " << block_height  << ", thread_idx = " << thread_idx << ", block vtx size = " << block.vtx.size() << std::endl;
//        TxIndexOnChain txIdx;
//        txIdx.Unserialize(recordedSentTxFile);
//        uint32_t oldCnt = cnt;
//        while (!recordedSentTxFile.eof() && txIdx.block_height == block_height){
//            if (ShutdownRequested())
//            break;
//            sendTx(block.vtx[txIdx.offset_in_block], txIdx.offset_in_block, block_height);
//            cnt++;
//            txIdx.Unserialize(recordedSentTxFile);
//        }
//	    std::cout << __func__ << ": thread " << thread_idx << " sent " << cnt - oldCnt << " tx in block " << block_height << std::endl;
//    }
//    /* send all tx remaining in batch buffers. */
//    g_pbft->sendAllBatch();
//    /* send all remaining tx in the queue.  */
//    //std::cout << "sending remaining tx" << std::endl;
//    //while (!listDelaySendingTx.empty()) {
//    //    /* sleep for a while to give depended tx enough time to finish. */
//    //    usleep(100);
//    //    if (ShutdownRequested())
//    //    	break;
//    //    cnt += sendQueuedTx(listDelaySendingTx);
//    //}
//    std::cout << __func__ << ": thread " << thread_idx << " sent " << cnt << " tx in total. " << std::endl;
//    totalTxSent += cnt; 
//}


bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height, std::vector<std::deque<TypedReq>>& batchBuffers, uint32_t& reqSentCnt) {
	/* get the input shards and output shards id*/
	TxPlacer txPlacer;
	CCoinsViewCache view(pcoinsTip.get());
	std::vector<int32_t> shards = txPlacer.randomPlace(*tx, view);
	const uint256& hashTx = tx->GetHash();

	assert((tx->IsCoinBase() && shards.size() == 1) || (!tx->IsCoinBase() && shards.size() >= 2)); // there must be at least one output shard and one input shard for non-coinbase tx.
	//std::cout << idx << "-th" << " tx "  <<  hashTx.GetHex().substr(0, 10) << " : ";
	//for (int shard : shards)
	//    std::cout << shard << ", ";
	//std::cout << std::endl;

	/* send tx and collect time info to calculate latency. 
	 * We also remove all reply msg for this req for resending aborted tx. */
	//g_pbft->replyMap[hashTx].clear();
	//g_pbft->mapTxStartTime.erase(hashTx);
	/* In closed loop test, for a tx has been send out, it will be committed and 
	 * the lastest_prereq_tx info is no longer need, so we can safely put a dummy
	 * value. 
	 */
        g_pbft->txInFly.insert(std::make_pair(hashTx, std::move(TxBlockInfo(tx, block_height, idx))));
	if ((shards.size() == 2 && shards[0] == shards[1]) || shards.size() == 1) {
	    /* this is a single shard tx */
	    g_pbft->add2Batch(shards[0], ClientReqType::TX, tx, batchBuffers[shards[0]]);
        reqSentCnt++;
	    if (shards.size() != 1) {
            /* only count non-coinbase tx b/c coinbase tx do not 
             * have the time-consuming sig verification step. */
            g_pbft->vLoad.add(shards[0], CPbft::LOAD_TX);
	    }
	} else {
	    /* this is a cross-shard tx */
	    for (uint i = 1; i < shards.size(); i++) {
            g_pbft->inputShardReplyMap[hashTx].lockReply.insert(std::make_pair(shards[i], std::vector<CInputShardReply>()));
            g_pbft->inputShardReplyMap[hashTx].decision.store('\0', std::memory_order_relaxed);
            g_pbft->add2Batch(shards[i], ClientReqType::LOCK, tx, batchBuffers[shards[i]]);
            reqSentCnt++;
            g_pbft->vLoad.add(shards[i], CPbft::LOAD_LOCK);
	    }
	}
	return true;
}

TxIndexOnChain::TxIndexOnChain(): block_height(0), offset_in_block(0) { }

TxIndexOnChain::TxIndexOnChain(uint32_t block_height_in, uint32_t offset_in_block_in):
 block_height(block_height_in), offset_in_block(offset_in_block_in) { }

bool TxIndexOnChain::IsNull() {
    return block_height == 0 && offset_in_block == 0;
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

std::string TxIndexOnChain::ToString() const {
    return "(" + std::to_string(block_height) + ", " + std::to_string(offset_in_block) + ")";
}

DependencyRecord::DependencyRecord(): tx(), prereq_tx() { }
DependencyRecord::DependencyRecord(const uint32_t block_height, const uint32_t offset_in_block, const TxIndexOnChain& latest_prereq_tx_in): tx(block_height, offset_in_block), prereq_tx(latest_prereq_tx_in) { }
