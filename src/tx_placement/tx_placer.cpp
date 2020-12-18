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
#include <fstream>
#include <algorithm>

std::atomic<uint32_t> totalTxSent(0);
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
bool buildWaitGraph = false;

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

TxPlacer::TxPlacer():totalTxNum(0), vTxCnt(num_committees, 0){ }
static uint32_t sendTxChunk(const CBlock& block, const uint block_height, const uint32_t start_tx, int noop_count, TxPlacer& txplacer, std::list<TxBlockInfo>& qDelaySendingTx);

int32_t TxPlacer::randomPlaceUTXO(const uint256& txid) {
    return txid.GetCheapHash() % num_committees;
}

std::vector<int32_t> TxPlacer::dependencyPlace(const CTransaction& tx, CCoinsViewCache& cache, std::vector<std::vector<uint32_t> >& vShardUtxoIdxToLock, const uint32_t block_height){
    std::vector<int32_t> ret;

    /* random place coinbase tx */
    if (tx.IsCoinBase()) { 
	return std::vector<int32_t>{randomPlaceUTXO(tx.GetHash())};
    }

    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t> > mapInputShardUTXO;
    /* key is shard id, value is the total value of input utxos in this shard. */
    std::map<int32_t, CAmount> mapInputShardValue;
    /* Add the input shard ids to the set, and assign the outputShard with the 
     * shard id of the first input UTXO. 
     * Note: some input UTXOs might not be in the coinsViewCache, so we maintain
     * a map<txid, shardId> to keep track of the shard id of newly processed tx 
     * during test.
     */
    //std::cout << " tx " << tx.GetHash().GetHex().substr(0,10) << " inputs : ";
    for(uint32_t i = 0; i < tx.vin.size(); i++) {
	//std::cout << "Outpoint (" << tx.vin[i].prevout.hash.GetHex().substr(0, 10) << ", " << tx.vin[i].prevout.n << "), ";
	assert(cache.HaveCoin(tx.vin[i].prevout));
	const Coin& coin = cache.AccessCoin(tx.vin[i].prevout);
	mapInputShardUTXO[coin.shardAffinity].push_back(i);
	mapInputShardValue[coin.shardAffinity] += coin.out.nValue;
    }
    //std::cout << std::endl;

    /* output shard is the shard host most-valued input UTXOs. */
    CAmount maxShardValue = 0;
    int32_t outputShard = -1;
    for (const auto& pair : mapInputShardValue) {
	if (pair.second > maxShardValue) {
	    maxShardValue = pair.second;
	    outputShard = pair.first;
	}
    }
    assert(outputShard >= 0 && outputShard < num_committees);
    
    /* Because this func is called by the 2PC coordinator, it should add the shard
     * info of this outputs of tx to coinsviewcache for future use. 
     */
    //std::cout << __func__ << ": add to mapTxShard, tx = " << tx.GetHash().GetHex().substr(0, 10) << ", outputShard = " << outputShard << std::endl;
    AddCoins(cache, tx, block_height, outputShard);

    /* inputShardIds.size() is the shard span of this tx. */
    shardCntMap[tx.vin.size()][mapInputShardUTXO.size()]++;
    
    /* prepare a resultant vector for return */
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard);// put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) 
    {
	ret.push_back(it->first);
	vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    
    return ret;
}

std::vector<int32_t> TxPlacer::smartPlace(const CTransaction& tx, CCoinsViewCache& cache, std::vector<std::vector<uint32_t> >& vShardUtxoIdxToLock, const uint32_t block_height){
    std::vector<int32_t> ret;

    /* random place coinbase tx */
    if (tx.IsCoinBase()) { 
	return std::vector<int32_t>{randomPlaceUTXO(tx.GetHash())};
    }

    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t> > mapInputShardUTXO;
    /* Add the input shard ids to the set, and assign the outputShard with the 
     * shard id of the first input UTXO. 
     * Note: some input UTXOs might not be in the coinsViewCache, so we maintain
     * a map<txid, shardId> to keep track of the shard id of newly processed tx 
     * during test.
     */
    //std::cout << " tx " << tx.GetHash().GetHex().substr(0,10) << " inputs : ";
    for(uint32_t i = 0; i < tx.vin.size(); i++) {
	//std::cout << "Outpoint (" << tx.vin[i].prevout.hash.GetHex().substr(0, 10) << ", " << tx.vin[i].prevout.n << "), ";
	assert(cache.HaveCoin(tx.vin[i].prevout));
	mapInputShardUTXO[cache.AccessCoin(tx.vin[i].prevout).shardAffinity].push_back(i);
    }
    //std::cout << std::endl;

    /* get output shard id */
    int32_t outputShard = -1;
    if (mapInputShardUTXO.size() > 1) {
        /* input UTXOs are not in the same shard, choose load-balancing path. */
        for (const auto& pair : mapInputShardUTXO) {
            g_pbft->vLoad.add(pair.first, CPbft::LOAD_LOCK);
        }
        outputShard = g_pbft->vLoad.minEleIndex();
        assert(outputShard >= 0 && outputShard < num_committees);
        g_pbft->vLoad.add(outputShard, CPbft::LOAD_COMMIT);
    } else {
        assert(mapInputShardUTXO.size() == 1);
        outputShard = cache.AccessCoin(tx.vin[0].prevout).shardAffinity;
		/* only count non-coinbase tx b/c coinbase tx do not have the time-consuming sig verification step. */
        g_pbft->vLoad.add(outputShard, CPbft::LOAD_TX);
    }
    
    /* Because this func is called by the 2PC coordinator, it should add the shard
     * info of this outputs of tx to coinsviewcache for future use. 
     */
    //std::cout << __func__ << ": add to mapTxShard, tx = " << tx.GetHash().GetHex().substr(0, 10) << ", outputShard = " << outputShard << std::endl;
    AddCoins(cache, tx, block_height, outputShard);

    /* inputShardIds.size() is the shard span of this tx. */
    shardCntMap[tx.vin.size()][mapInputShardUTXO.size()]++;
    
    /* prepare a resultant vector for return */
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard);// put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) 
    {
	ret.push_back(it->first);
	vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    
    /*update tx counter. */
    vTxCnt[outputShard]++;
    int32_t hashingPlaceRes = randomPlaceUTXO(tx.GetHash());
    assert(hashingPlaceRes >= 0 && hashingPlaceRes < num_committees);
    vTxCnt[hashingPlaceRes]--;
    return ret;
}

/*
 * Strictly speaking, the return value should be either -1 or our shard id, but
 * as we use the entire chainstate at block 600999 in our test, we also have 
 * UTXOs not belonging to our shard in our coinsview.
 */
int32_t TxPlacer::smartPlaceUTXO(const COutPoint& txin, const CCoinsViewCache& cache) {
    if (!cache.HaveCoin(txin)) {
	/* this is a coin generated during testing and not belong to our shard. */
	return -2;
    } else {
	return cache.AccessCoin(txin).shardAffinity;
    }
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

static uint32_t sendQueuedTx(std::list<TxBlockInfo>& listDelaySendingTx, const int noop_count, TxPlacer& txplacer) {
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
	    txplacer.sendTx(iter->tx, iter->n, iter->blockHeight, true);
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

void assignShardAffinity(){
    std::cout << "Assigning shard affinity for all UTXOs..." << std::endl;
    std::map<uint, uint> affinityCntMap; // key is shard count, value is tx count
    std::unordered_map<uint256, int32_t, BlockHasher> tx2affinityMap;
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
    assert(pcursor);

    // iterate the chain state database
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
	COutPoint key;
        if (!pcursor->GetKey(key)) {
            std::cout << __func__ << ": unable to read key" << std::endl;
	    continue;
        }
        Coin coin;
        if (pcursor->GetValue(coin)) {
	    if (tx2affinityMap.find(key.hash) != tx2affinityMap.end()) {
		/* we have assigned affinity to other outputs of this tx,
		 * we must use the same shard affinity for this UTXO. */
		coin.shardAffinity = tx2affinityMap[key.hash];
		affinityCntMap[coin.shardAffinity]++;
		pcoinsTip->AddCoin(key, std::move(coin), true);

	    } else {
		/* this is the first output of its tx that we have ever seen.*/
		lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
		tx2affinityMap[key.hash] = lastAssignedAffinity;
		coin.shardAffinity = lastAssignedAffinity;
		affinityCntMap[coin.shardAffinity]++;
		pcoinsTip->AddCoin(key, std::move(coin), true);
    //	    std::cout << "chain affinity of " << key.ToString() 
    //		    << " = " << coin.chainAffinity 
    //		    << std::endl;
	    }
        } else {
            std::cout << __func__ << ": unable to read coin" << std::endl;
        }
        pcursor->Next();
    }
    pcoinsTip->Flush();

    std::cout << "chain affinity stats : " << std::endl;
    for(auto entry: affinityCntMap) {
	std::cout << "affinity = " << entry.first << " tx count : "
		<< entry.second << std::endl;
    }
}

void incrementalAssignShardAffinity(){
//    std::cout << "Incremental assigning shard affinity for no-affinity UTXOs..." << std::endl;
//    std::map<uint, uint> affinityCntMap; // key is shard count, value is tx count
//    std::queue<COutPoint> noAffinityUTXOs;
//    
//    uint nTotalUTXO = 0;
//
//    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
//    assert(pcursor);
//
//    // iterate the chain state database
//    while (pcursor->Valid()) {
//        boost::this_thread::interruption_point();
//	COutPoint key;
//        if (!pcursor->GetKey(key)) {
//            std::cout << __func__ << ": unable to read key" << std::endl;
//	    continue;
//        }
//	nTotalUTXO++;
//        const Coin& coin =  pcoinsTip->AccessCoin(key);
//	if(coin.shardAffinity != -1){
//	    affinityCntMap[coin.shardAffinity]++;
//	} else {
//	    noAffinityUTXOs.push(key);
//	}
//        pcursor->Next();
//    }
//
//    /* for shards having UTXOs less than the average number (ceiling),
//     * bulky assign UTXOs to it to make up for the lacking part. Do this until 
//     * noAffinityUTXOs is empty. We should end up with the number of UTXOs in 
//     * each other nearly the same */
//    uint nAvgUTXO = (nTotalUTXO + num_committees - 1) / num_committees; 
//    for (std::pair<const uint,uint>& entry: affinityCntMap){
//	if (entry.second < nAvgUTXO) {
//	    for (uint i = 0; i < nAvgUTXO - entry.second && !noAffinityUTXOs.empty(); i++) {
//		const Coin& coin =  pcoinsTip->AccessCoin(noAffinityUTXOs.front());
//		const_cast<Coin&>(coin).shardAffinity = entry.first; 
//		noAffinityUTXOs.pop();
//		entry.second++;
//	    }
//	    if (noAffinityUTXOs.empty())
//		break;
//	}
//    }
//    
//    pcoinsTip->Flush();
//
//    std::cout << "chain affinity stats : " << std::endl;
//    std::cout << "total: " << nTotalUTXO << std::endl;
//    for(auto entry: affinityCntMap) {
//	std::cout << "affinity = " << entry.first << " count : "
//		<< entry.second << std::endl;
//    }
}

void printShardAffinity(){
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
    assert(pcursor);

    std::map<uint, uint> affinityCntMap; // key is shard count, value is tx count
    // iterate the chain state database
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
	COutPoint key;
        if (!pcursor->GetKey(key)) {
            std::cout << __func__ << ": unable to read key" << std::endl;
	    continue;
        }
        Coin coin;
        if (pcursor->GetValue(coin)) {
//	    std::cout << "chain affinity of " << key.ToString() 
//		    << " = " << coin.chainAffinity 
//		    << std::endl;
	    affinityCntMap[coin.shardAffinity]++;
        } else {
            std::cout << __func__ << ": unable to read coin" << std::endl;
        }
        pcursor->Next();
    }
    std::cout << "chain affinity stats : " << std::endl;
    for(auto entry: affinityCntMap) {
	std::cout << "affinity = " << entry.first << " UTXO count : "
		<< entry.second << std::endl;
    }
}

//void smartPlaceTxInBlocks(){
//    TxPlacer txPlacer;
//    std::map<uint, uint> shardCntMap; // key is shard count, value is tx count
//    uint totalTxNum = 0;
//    for (uint i = blockStart; i < blockEnd; i++){
//	CBlockIndex* pblockindex = chainActive[i];
//	totalTxNum += pblockindex->nTx - 1; // exclude the coinbase tx
//	CBlock block;
//	if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
//	    // Block not found on disk. This could be because we have the block
//	    // header in our index but don't have the block (for example if a
//	    // non-whitelisted node sends us an unrequested long chain of valid
//	    // blocks, we add the headers to our index, but don't accept the
//	    // block).
//	    std::cerr << "Block not found on disk" << std::endl;
//	
//	/* start from the second transaction to exclude coinbase tx */
//	for (uint j = 1; j < block.vtx.size(); j++) {
////	    std::cout << txPlacer.randomPlaceTxid(block.vtx[j])
////		    << "-shard tx: " << block.vtx[j]->GetHash().GetHex()  << std::endl;
//
//	    //shardCntMap[txPlacer.randomPlaceTxid(block.vtx[j])]++;
//	    shardCntMap[txPlacer.smartPlace(block.vtx[j])]++;
//	}
//    }
//    std::cout << "total tx num = " << totalTxNum << std::endl;
//    std::cout << "tx shard num stats : " << std::endl;
//    for(auto entry: shardCntMap) {
//	std::cout << entry.first << "-shard tx count = "
//		<< entry.second << std::endl;
//    }
//}


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
	//std::cout << " block_height = " << block_height  << ", thread_idx = " << thread_idx << ", block vtx size = " << block.vtx.size() << std::endl;
        txPlacer.loadShardInfo(block_height);

	for (size_t i = thread_idx * txChunkSize; i < block.vtx.size(); i += jump_length){
	    //std::cout << __func__ << ": thread " << thread_idx << " sending No." << i << " tx in block " << block_height << std::endl;
	    uint32_t actual_chunk_size = sendTxChunk(block, block_height, i, noop_count, txPlacer, listDelaySendingTx);
	    cnt += actual_chunk_size;
	    //std::cout << __func__ << ": thread " << thread_idx << " sent " << actual_chunk_size << " tx in block " << block_height << std::endl;
	}

	/* check delay sending tx.  */
        cnt += sendQueuedTx(listDelaySendingTx, noop_count, txPlacer);
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

static uint32_t sendTxChunk(const CBlock& block, const uint block_height, const uint32_t start_tx, const int noop_count, TxPlacer& txplacer, std::list<TxBlockInfo>& listDelaySendingTx) {
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
		    //std::cout << "delay sending tx (" << block_height << ", " << j << ") . current listDelaySendingTx size = " << listDelaySendingTx.size() << std::endl;
                    listDelaySendingTx.emplace_back(block.vtx[j], block_height, j);
		    txplacer.mapDelayedTxShardInfo.emplace(txIdx, txplacer.vShardInfo[j]);
		    prereqTxCleared = false;
                    break;
                }
            }
	}
	if (prereqTxCleared) {
	    txplacer.sendTx(block.vtx[j], j, block_height);
	    cnt++;
	    /* delay by doing noop. */
	    delayByNoop(noop_count);
	} 
    }
    return cnt;
}

uint32_t TxPlacer::sendAllTailTx(int noop_count) {
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

bool TxPlacer::sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height, const bool isDelayedTx) {
	const uint256& hashTx = tx->GetHash();
	/* get the input shards and output shards id*/
	const ShardInfo* pShardInfo;
	std::map<TxIndexOnChain, ShardInfo>::iterator it;
	if (!isDelayedTx) {
	    pShardInfo = &vShardInfo[idx];
	} else {
		
	    it = mapDelayedTxShardInfo.find(TxIndexOnChain(block_height, idx));
	    pShardInfo = &(it->second);
	}

	const std::vector<int32_t>& shards = pShardInfo->shards;

	const std::vector<std::vector<uint32_t> >& vShardUtxoIdxToLock = pShardInfo->vShardUtxoIdxToLock;

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
	g_pbft->txInFly.insert(std::make_pair(hashTx, std::move(TxBlockInfo(tx, block_height, idx, shards[0]))));
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
		LockReq lockReq(*tx, vShardUtxoIdxToLock[i - 1]);
		g_connman->PushMessage(g_pbft->leaders[shards[i]], msgMaker.Make(NetMsgType::OMNI_LOCK, lockReq));
		g_pbft->vLoad.add(shards[i], CPbft::LOAD_LOCK);
	    }
	}
	
	if (isDelayedTx) {
	    mapDelayedTxShardInfo.erase(it);
	}

	return true;
}

void buildDependencyGraph(uint32_t block_height) {
    CBlock block;
    CBlockIndex* pblockindex = chainActive[block_height];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        std::cerr << "Block not found on disk" << std::endl;
    }
    std::cout << __func__ << ": resolve dependency for block " << block_height << std::endl;
    std::unordered_set<uint256, BlockHasher> txid_set;
    std::unordered_map<uint256, WaitInfo, BlockHasher> waitForGraph; // <txid, prerequiste tx list>
    for (uint j = 0; j < block.vtx.size(); j++) {
	CTransactionRef tx = block.vtx[j]; 
	const uint256& hashTx = tx->GetHash();
	for (const CTxIn& utxoIn:  tx->vin) {
	    const uint256& prereqTxid = utxoIn.prevout.hash;
	    std::unordered_set<uint256, BlockHasher>::const_iterator got = txid_set.find(prereqTxid);
	    if (got != txid_set.end()) {
		waitForGraph[hashTx].prereqTxSet.insert(prereqTxid);
	    }

	}
	if (waitForGraph.find(hashTx) != waitForGraph.end()){
	    waitForGraph[hashTx].idx = j;
	}
	txid_set.insert(hashTx);
    }

    // print waitForGraph
    for (const auto& entry: waitForGraph) {
	std::cout << entry.first.GetHex() << ": ";
	std::cout << entry.second.idx << ", ";
	for (const auto& prereqTxid: entry.second.prereqTxSet) {
	    std::cout << prereqTxid.GetHex() << ", ";
	}
	std::cout << std::endl;
    }
}

//void smartPlaceTxInBlock(const std::shared_ptr<const CBlock> pblock){
//    std::cout << "SMART place block " << chainActive.Height() + 1 << std::endl;
//    TxPlacer txPlacer;
//    totalTxNum += pblock->vtx.size() - 1; // exclude the coinbase tx
//    /* start from the second transaction to exclude coinbase tx */
//    for (uint j = 1; j < pblock->vtx.size(); j++) {
//	shardCntMap[pblock->vtx[j]->vin.size()][txPlacer.smartPlace(pblock->vtx[j])]++;
//    }
//}

void TxPlacer::loadShardInfo(int block_height) {
    CBlock block;
    CBlockIndex* pblockindex = chainActive[block_height];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        std::cerr << "Block not found on disk" << std::endl;
    }
    std::cout << __func__ << ": loading shard info for " << block.vtx.size() << " tx in block " << block_height << std::endl;
    std::ifstream shardInfoFile;
    shardInfoFile.open(getShardInfoFilename(block_height));
    assert(shardInfoFile.is_open());
    /* we did not clear vShardInfo b/c it will be overwirtten during file unserialization. */
    vShardInfo.resize(block.vtx.size());
    for (uint i = 0; i < vShardInfo.size(); i++) {
	vShardInfo[i].Unserialize(shardInfoFile);
    }
    shardInfoFile.close();
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
