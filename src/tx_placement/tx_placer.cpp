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
#include "pbft/pbft.h"
#include <thread>
#include <chrono>
#include <time.h>
#include "txdb.h"

static const uint32_t SEC = 1000000; // 1 sec = 10^6 microsecond

uint32_t num_committees;
int lastAssignedAffinity = -1;
//uint32_t txStartBlock;
//uint32_t txEndBlock;
bool buildWaitGraph = false;

TxPlacer::TxPlacer():totalTxNum(0){}


/* all output UTXOs of a tx is stored in one shard. */
std::vector<int32_t> TxPlacer::randomPlace(const CTransaction& tx){
	    std::set<int> inputShardIds;

    /* add the input shard ids to the set */
    if (!tx.IsCoinBase()) { // do not calculate input shard for coinbase.
	    for(uint32_t i = 0; i < tx.vin.size(); i++) {
	    arith_uint256 txid = UintToArith256(tx.vin[i].prevout.hash);
	    arith_uint256 quotient = txid / num_committees;
	    arith_uint256 inShardId = txid - quotient * num_committees;
	    inputShardIds.insert((int)(inShardId.GetLow64()));
	}
    }

//    std::cout << "tx " << tx->GetHash().GetHex() << " spans shards : ";
//    for(auto entry : shardIds) {
//	std::cout << entry << " ";
//    }
//    std::cout << std::endl;

    /* add the output shard id to the above set */
    arith_uint256 txid = UintToArith256(tx.GetHash());
    arith_uint256 quotient = txid / num_committees;
    arith_uint256 outShardId = txid - quotient * num_committees;
    if (inputShardIds.find((int)(outShardId.GetLow64())) != inputShardIds.end()) {
	/* inputShardIds.size() is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size()]++;
    } else {
	/* inputShardIds.size() + 1 is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size() + 1]++;
    }
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret(inputShardIds.size() + 1);
    ret[0] = (int32_t)(outShardId.GetLow64());// put the outShardIt as the first element
    std::copy(inputShardIds.begin(), inputShardIds.end(), ret.begin() + 1);
    return ret;
}

int32_t TxPlacer::randomPlaceUTXO(const uint256& txid) {
	arith_uint256 txid_arth = UintToArith256(txid);
	arith_uint256 quotient = txid_arth / num_committees;
	arith_uint256 inShardId = txid_arth - quotient * num_committees;
	return (int32_t)(inShardId.GetLow64());
}

std::vector<int32_t> TxPlacer::smartPlace(const CTransaction& tx, CCoinsViewCache& cache, std::vector<std::vector<uint32_t> >& vShardUtxoIdxToLock){
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
	if(!cache.HaveCoin(tx.vin[i].prevout)) {
	    return ret;
	}
	mapInputShardUTXO[cache.AccessCoin(tx.vin[i].prevout).shardAffinity].push_back(i);
    }
    //std::cout << std::endl;

    /* get output shard id */
    int32_t outputShard = cache.AccessCoin(tx.vin[0].prevout).shardAffinity;
    
    /* Because this func is called by the 2PC coordinator, it should add the shard
     * info of this outputs of tx to coinsviewcache for future use. 
     */
    //std::cout << __func__ << ": add to mapTxShard, tx = " << tx.GetHash().GetHex().substr(0, 10) << ", outputShard = " << outputShard << std::endl;
    bool flushed = cache.Flush(); // flush to pcoinsTip
    assert(flushed);

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


//uint32_t sendTxInBlock(uint32_t block_height, struct timeval& expected_last_send_time, int txSendPeriod) {
uint32_t sendTxInBlock(uint32_t block_height, int txSendPeriod) {
    CBlock block;
    CBlockIndex* pblockindex = chainActive[block_height];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        std::cerr << "Block not found on disk" << std::endl;
    }
    std::cout << __func__ << ": sending " << block.vtx.size() << " tx in block " << block_height << std::endl;
    /* */
    const struct timespec sleep_length = {0, txSendPeriod * 1000};
    uint32_t cnt = 0;
    for (uint32_t j = 0; j < block.vtx.size(); j++) {
	CTransactionRef tx = block.vtx[j]; 
	const uint256& hashTx = tx->GetHash();
	if (sendTx(block.vtx[j], j, block_height)){
	    cnt++;
	}
	nanosleep(&sleep_length, NULL);

	if (cnt & 0x1F == 0) {
	    while (pcoinsTip->HaveInputs(*(g_pbft->txDelaySendQueue.front().tx))) {
		TxBlockInfo& txInfo = g_pbft->txDelaySendQueue.front();
		assert(sendTx(txInfo.tx, txInfo.blockHeight, txInfo.n));
		cnt++;
		g_pbft->txDelaySendQueue.pop();
		nanosleep(&sleep_length, NULL);
	    }
	}

	/*-----calculate how long we should sleep------*/
//	struct timeval current;
//	struct timespec sleep_length = {0, 0};
//	if (expected_last_send_time.tv_usec + txSendPeriod > SEC){
//	    /* we assume that (expected_last_send_time.tv_usec + txSendPeriod) < 2 SEC */
//	    expected_last_send_time.tv_sec++;
//	    expected_last_send_time.tv_usec = expected_last_send_time.tv_usec + txSendPeriod - SEC;
//	} else {
//	    expected_last_send_time.tv_usec += txSendPeriod;
//	}
//	/* now expected_last_send_time becomes the time at which we should send this tx. */
//	gettimeofday(&current, NULL);
//	if (current.tv_sec < expected_last_send_time.tv_sec || (current.tv_sec == expected_last_send_time.tv_sec && current.tv_usec < expected_last_send_time.tv_usec)) {
//	    /* we are not reach the expected sending time yet, so sleep for a while.
//	     * we assume that this thread should never sleep more than 1 second. */
//	    sleep_length.tv_nsec = (expected_last_send_time.tv_sec - current.tv_sec) * SEC * 1000
//		    + (expected_last_send_time.tv_usec - current.tv_usec) * 1000;
//	    nanosleep((const timespec*)&sleep_length, NULL);
//	    std::cout << __func__ << ": slept " << sleep_length.tv_sec << "s " 
//		    << sleep_length.tv_nsec << "ns" << std::endl;
//	} else {
//	    std::cout << __func__ << ": do not sleep" << std::endl;
//	}


    }

    /* We have sent all tx but those waiting for prerequisite tx. Poll the priority 
     * queue to see if some dependent tx are ready until we sent all tx. */
    std::cout << "sending tail tx ... " << std::endl;
    uint32_t alreadySentCnt = cnt;
    while (cnt < block.vtx.size()) {
	for (auto clearedDependentTx: g_prereqClearTxPQ->pop_upto(block.vtx.size() - 1)) {
	    sendTx(block.vtx[clearedDependentTx], clearedDependentTx, block_height);
	    cnt++;
	    nanosleep(&sleep_length, NULL);
	}
	usleep(1000); // sleep for 1ms before the next pq check
    }
    std::cout << cnt - alreadySentCnt << "tail tx are sent. " << std::endl;
    return cnt;
}

bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height) {
	TxPlacer txPlacer;
	CCoinsViewCache view(pcoinsTip.get());
	const uint256& hashTx = tx->GetHash();
	/* get the input shards and output shards id*/
	std::vector<std::vector<uint32_t> > vShardUtxoIdxToLock;
	std::vector<int32_t> shards = txPlacer.smartPlace(*tx, view, vShardUtxoIdxToLock);
	if (shards.empty()) {
	    /* some inputs are missing. Put this tx in a queue for later sending. */
	    std::cout << idx << "-th" << " tx "  <<  hashTx.GetHex().substr(0, 10) << " is queued. " << std::endl;
	    g_pbft->txDelaySendQueue.emplace(tx, block_height, idx);
	    return false;
	}

	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	assert((tx->IsCoinBase() && shards.size() == 1) || (!tx->IsCoinBase() && shards.size() >= 2)); // there must be at least one output shard and one input shard for non-coinbase tx.
	std::cout << idx << "-th" << " tx "  <<  hashTx.GetHex().substr(0, 10) << " : ";
	for (int shard : shards)
	    std::cout << shard << ", ";
	std::cout << std::endl;
	g_pbft->replyMap[hashTx].clear();
	g_pbft->txInFly.insert(std::make_pair(hashTx, std::move(TxBlockInfo(tx, block_height, idx))));
	g_pbft->mapTxStartTime.erase(hashTx);
	struct TxStat stat;
	if ((shards.size() == 2 && shards[0] == shards[1]) || shards.size() == 1) {
	    /* this is a single shard tx */
	    stat.type = TxType::SINGLE_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    g_connman->PushMessage(g_pbft->leaders[shards[0]], msgMaker.Make(NetMsgType::PBFT_TX, *tx));
	} else {
	    /* this is a cross-shard tx */
	    stat.type = TxType::CROSS_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    for (uint i = 1; i < shards.size(); i++) {
		g_pbft->inputShardReplyMap[hashTx].lockReply.insert(std::make_pair(shards[i], std::vector<CInputShardReply>()));
		g_pbft->inputShardReplyMap[hashTx].decision = '\0';
		LockReq lockReq(*tx, vShardUtxoIdxToLock[i - 1]);
		g_connman->PushMessage(g_pbft->leaders[shards[i]], msgMaker.Make(NetMsgType::OMNI_LOCK, lockReq));
	    }
	}
	return true;
}

void buildDependencyGraph(uint32_t block_height, std::map<uint32_t, std::unordered_set<uint256, BlockHasher>>& waitForGraph) {
    CBlock block;
    CBlockIndex* pblockindex = chainActive[block_height];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        std::cerr << "Block not found on disk" << std::endl;
    }
    std::cout << __func__ << ": resolve dependency for block " << block_height << std::endl;
    std::unordered_set<uint256, BlockHasher> txid_set;
    for (uint32_t j = 0; j < block.vtx.size(); j++) {
	CTransactionRef tx = block.vtx[j]; 
	for (const CTxIn& utxoIn:  tx->vin) {
	    const uint256& prereqTxid = utxoIn.prevout.hash;
	    std::unordered_set<uint256, BlockHasher>::const_iterator got = txid_set.find(prereqTxid);
	    if (got != txid_set.end()) {
		waitForGraph[j].insert(prereqTxid);
	    }
	}
	const uint256& hashTx = tx->GetHash();
	txid_set.insert(hashTx);
    }

    // print waitForGraph
    for (const auto& entry: waitForGraph) {
	std::cout << entry.first << ": ";
	for (const auto& prereqTxid: entry.second) {
	    std::cout << prereqTxid.GetHex() << ", ";
	}
	std::cout << std::endl;
    }
}

ThreadSafeIntPQ::ThreadSafeIntPQ(){ };

void ThreadSafeIntPQ::push(const uint32_t val){
    std::lock_guard<std::mutex> lock(mtx_);
    pq_.push(val);
}

std::vector<uint32_t> ThreadSafeIntPQ::pop_upto(const uint32_t upto){
    std::vector<uint32_t> ret;
    std::unique_lock<std::mutex> lock(mtx_);
    while (!pq_.empty() && pq_.top() <= upto) {
	ret.push_back(pq_.top());
	pq_.pop();
    }
    lock.unlock();
    return ret;
}

std::unique_ptr<ThreadSafeIntPQ> g_prereqClearTxPQ; // a priority queue for prerequiste-tx-clear dependent tx
std::unique_ptr<std::map<uint32_t, std::unordered_set<uint256, BlockHasher>>> g_waitForGraph;

//void smartPlaceTxInBlock(const std::shared_ptr<const CBlock> pblock){
//    std::cout << "SMART place block " << chainActive.Height() + 1 << std::endl;
//    TxPlacer txPlacer;
//    totalTxNum += pblock->vtx.size() - 1; // exclude the coinbase tx
//    /* start from the second transaction to exclude coinbase tx */
//    for (uint j = 1; j < pblock->vtx.size(); j++) {
//	shardCntMap[pblock->vtx[j]->vin.size()][txPlacer.smartPlace(pblock->vtx[j])]++;
//    }
//}
