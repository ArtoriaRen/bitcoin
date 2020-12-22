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
#include "chainparams.h"
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


static uint32_t sendTxChunk(const CBlock& block, const uint block_height, const uint32_t start_tx, int noop_count, std::list<TxBlockInfo>& qDelaySendingTx);

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

static uint32_t sendQueuedTx(std::list<TxBlockInfo>& listDelaySendingTx, const int noop_count) {
    int txSentCnt = 0;
    auto& mapDependency = g_pbft->mapDependency; 
    auto iter = listDelaySendingTx.begin();
    while (iter != listDelaySendingTx.end()) {
	if (ShutdownRequested())
	    return txSentCnt;
        const TxIndexOnChain txIdx(iter->blockHeight, iter->n);
	bool prereqTxCleared = true;
        for (auto& prereqTx: mapDependency[txIdx] ) {
            if (g_pbft->uncommittedPrereqTxSet.haveTx(prereqTx)) {
		prereqTxCleared = false;
                break;
            }
        }
	if (prereqTxCleared) {
	    /* All prereqTx have been committed, ready to send this tx. */
	    //std::cout << "sending queued tx (" << iter->blockHeight << ", " << iter->n << ")" << std::endl;
	    sendTx(iter->tx, iter->n, iter->blockHeight);
	    iter = listDelaySendingTx.erase(iter);
	    txSentCnt++;
	    delayByNoop(noop_count);
	} else {
	    iter++;
	}
    }
    //std::cout << __func__ << " sent " <<  txSentCnt << " queued tx. queue size = "  << listDelaySendingTx.size() << std::endl;
    return txSentCnt;
}


void sendTxOfThread(const int startBlock, const int endBlock, const uint32_t thread_idx, const uint32_t num_threads, const int noop_count) {
    RenameThread(("sendTx" + std::to_string(thread_idx)).c_str());
    uint32_t cnt = 0;
    const uint32_t jump_length = num_threads * txChunkSize;
    std::list<TxBlockInfo> listDelaySendingTx;
    TxPlacer txPlacer;
    for (int block_height = startBlock; block_height < endBlock; block_height++) {
	if (ShutdownRequested())
	    break;

	CBlock block;
	CBlockIndex* pblockindex = chainActive[block_height];
	if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
	    std::cerr << "Block not found on disk" << std::endl;
	}
	std::cout << " block_height = " << block_height  << ", thread_idx = " << thread_idx << ", block vtx size = " << block.vtx.size() << std::endl;
	for (size_t i = thread_idx * txChunkSize; i < block.vtx.size(); i += jump_length){
	    std::cout << __func__ << ": thread " << thread_idx << " sending No." << i << " tx in block " << block_height << std::endl;
	    uint32_t actual_chunk_size = sendTxChunk(block, block_height, i, noop_count, listDelaySendingTx);
	    cnt += actual_chunk_size;
	    std::cout << __func__ << ": thread " << thread_idx << " sent " << actual_chunk_size << " tx in block " << block_height << std::endl;
	}

	/* check delay sending tx.  */
        cnt += sendQueuedTx(listDelaySendingTx, noop_count);
    }
    /* send all remaining tx in the queue.  */
    //std::cout << "sending remaining tx" << std::endl;
    //while (!listDelaySendingTx.empty()) {
    //    /* sleep for a while to give depended tx enough time to finish. */
    //    usleep(100);
    //    if (ShutdownRequested())
    //    	break;
    //    cnt += sendQueuedTx(listDelaySendingTx);
    //}
    std::cout << __func__ << ": thread " << thread_idx << " sent " << cnt << " tx in total. queue size = "  << listDelaySendingTx.size() << std::endl;
    totalTxSent += cnt; 
}

static uint32_t sendTxChunk(const CBlock& block, const uint block_height, const uint32_t start_tx, const int noop_count, std::list<TxBlockInfo>& listDelaySendingTx) {
    uint32_t cnt = 0;
    uint32_t end_tx = std::min(start_tx + txChunkSize, block.vtx.size());
    auto& mapDependency = g_pbft->mapDependency; 

    for (uint j = start_tx; j < end_tx; j++) {
	if (ShutdownRequested())
	    break;

	TxIndexOnChain txIdx(block_height,j);
	auto it = mapDependency.find(txIdx);
	bool prereqTxCleared = true;
	if (it != mapDependency.end()) {
            for (auto& prereqTx: it->second) {
                if (g_pbft->uncommittedPrereqTxSet.haveTx(prereqTx)) {
                    /* the prereq tx has not been committed yet, enqueue and send later */
		    //std::cout << "delay sending tx (" << block_height << ", " << j << ") " << std::endl;
                    listDelaySendingTx.emplace_back(block.vtx[j], block_height, j);
		    prereqTxCleared = false;
                    break;
                }
            }
	}
	if (prereqTxCleared) {
	    sendTx(block.vtx[j], j, block_height);
	    cnt++;
	    /* delay by doing noop. */
	    delayByNoop(noop_count);
	} 
    }
    return cnt;
}

uint32_t sendAllTailTx(int noop_count) {
    /* We have sent all tx but those waiting for prerequisite tx. Poll the 
     * queue to see if some dependent tx are ready until we sent all tx. */
    std::cout << "sending " << g_pbft->txResendQueue.size() << " tail tx ... " << std::endl;
    uint32_t cnt = 0;
    while (!g_pbft->txResendQueue.empty()) {
	if (ShutdownRequested())
	    break;

	TxBlockInfo& txInfo = g_pbft->txResendQueue.front();
	sendTx(txInfo.tx, txInfo.n, txInfo.blockHeight);
	g_pbft->txResendQueue.pop_front();
	cnt++;
	/* delay by doing noop. */
	delayByNoop(noop_count);
    }
    return cnt;
}

bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height) {
	/* get the input shards and output shards id*/
	TxPlacer txPlacer;
	CCoinsViewCache view(pcoinsTip.get());
	std::vector<int32_t> shards = txPlacer.randomPlace(*tx, view);
	const uint256& hashTx = tx->GetHash();

	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
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
	struct TxStat stat;
	if ((shards.size() == 2 && shards[0] == shards[1]) || shards.size() == 1) {
	    /* this is a single shard tx */
	    stat.type = TxType::SINGLE_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    g_connman->PushMessage(g_pbft->leaders[shards[0]], msgMaker.Make(NetMsgType::PBFT_TX, *tx));
	    if (shards.size() != 1) {
		/* only count non-coinbase tx b/c coinbase tx do not have the time-consuming sig verification step. */
		g_pbft->vLoad.add(shards[0], CPbft::LOAD_TX);
	    }
	} else {
	    /* this is a cross-shard tx */
	    stat.type = TxType::CROSS_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    for (uint i = 1; i < shards.size(); i++) {
		g_pbft->inputShardReplyMap[hashTx].lockReply.insert(std::make_pair(shards[i], std::vector<CInputShardReply>()));
		g_pbft->inputShardReplyMap[hashTx].decision.store('\0', std::memory_order_relaxed);
		g_connman->PushMessage(g_pbft->leaders[shards[i]], msgMaker.Make(NetMsgType::OMNI_LOCK, *tx));
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
