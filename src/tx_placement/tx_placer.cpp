/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <tx_placement/tx_placer.h>
#include <queue>

#include "hash.h"
#include "arith_uint256.h"
#include "chain.h"
#include "validation.h"
#include "chainparams.h"
#include "txdb.h"


/* global variable configurable from conf file. */
uint32_t randomPlaceBlock;
uint32_t blockEnd;
uint32_t num_committees;
int lastAssignedAffinity = -1;

TxPlacer::TxPlacer():totalTxNum(0){}


/* all output UTXOs of a tx is stored in one shard. */
std::vector<int32_t> TxPlacer::randomPlace(const CTransaction& tx){
    std::set<int> inputShardIds;
    
    /* add the input shard ids to the set */
    for(uint32_t i = 0; i < tx.vin.size(); i++) {
	arith_uint256 txid = UintToArith256(tx.vin[i].prevout.hash);
	arith_uint256 quotient = txid / num_committees;
	arith_uint256 inShardId = txid - quotient * num_committees;
	inputShardIds.insert((int)(inShardId.GetLow64()));
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

int32_t TxPlacer::smartPlace(const CTransaction& tx, CCoinsViewCache& cache){
//    int32_t outputShard = -1;
//    std::set<int> shardIds;
//    const Coin& firstUtxoIn = cache.AccessCoin(tx.vin[0].prevout);
//    /* if the coin is not found, an empty coin with null CTxOut is returned. */
//    if (firstUtxoIn.IsSpent()) {
//	std::cerr << "not found first input: " << tx.vin[0].prevout.ToString() << std::endl;
//    }
//
//    /* add output shard id */
//    if(firstUtxoIn.shardAffinity != -1) {
//	outputShard = firstUtxoIn.shardAffinity;
//	shardIds.insert(firstUtxoIn.shardAffinity);
//    } else {
//	/* the first utxo has not been assigned chainAffinity. Assign it now, and
//	 * the all output UTXOs should be with the same chainAffinity. */
//	lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
//	outputShard = lastAssignedAffinity;
//	shardIds.insert(lastAssignedAffinity);
//    }
//    /* add input shard id. (The shard of the first input UXTO has already been 
//     * considered when adding output shard id, so here we start with the second
//     * input UTXO.)*/
//    for(uint32_t i = 1; i < tx.vin.size(); i++) {
//	const Coin& utxoIn =  cache.AccessCoin(tx.vin[i].prevout);
//	if(utxoIn.shardAffinity != -1) {
//	    shardIds.insert(utxoIn.shardAffinity);
//	} else {
//	    /* the first utxo has not been assigned chainAffinity. Assign it now, and
//	     * the all output UTXOs should be with the same chainAffinity. */
//	    lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
//	    shardIds.insert(lastAssignedAffinity);
//	}
//    }
//    /* shardIds.size() is the shard span of this tx. */
//    shardCntMap[tx.vin.size()][shardIds.size()]++;
//    return outputShard;
    return -1;
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

/* UTXOs produced by the same transaction should be in the same shard. Otherwise,
 * we may be worse than random placement for cross-shard tx because even random
 * placement put UTXOs of the same tx in the same shard. */
void assignShardAffinity(){
//    std::cout << "Assigning shard affinity for all UTXOs..." << std::endl;
//    std::map<uint, uint> affinityCntMap; // key is shard count, value is tx count
//    std::unordered_map<uint256, int32_t, BlockHasher> tx2affinityMap;
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
//        Coin coin;
//        if (pcursor->GetValue(coin)) {
//	    if (tx2affinityMap.find(key.hash) != tx2affinityMap.end()) {
//		/* we have assigned affinity to other outputs of this tx,
//		 * we must use the same shard affinity for this UTXO. */
//		coin.shardAffinity = tx2affinityMap[key.hash];
//		pcoinsTip->AddCoin(key, std::move(coin), true);
//
//	    } else {
//		/* this is the first output of its tx that we have ever seen.*/
//		lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
//		tx2affinityMap[key.hash] = lastAssignedAffinity;
//		affinityCntMap[lastAssignedAffinity]++;
//		coin.shardAffinity = lastAssignedAffinity;
//		pcoinsTip->AddCoin(key, std::move(coin), true);
//    //	    std::cout << "chain affinity of " << key.ToString() 
//    //		    << " = " << coin.chainAffinity 
//    //		    << std::endl;
//	    }
//        } else {
//            std::cout << __func__ << ": unable to read coin" << std::endl;
//        }
//        pcursor->Next();
//    }
//    pcoinsTip->Flush();
//
//    std::cout << "chain affinity stats : " << std::endl;
//    for(auto entry: affinityCntMap) {
//	std::cout << "affinity = " << entry.first << " tx count : "
//		<< entry.second << std::endl;
//    }
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
//    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
//    assert(pcursor);
//
//    std::map<uint, uint> affinityCntMap; // key is shard count, value is tx count
//    // iterate the chain state database
//    while (pcursor->Valid()) {
//        boost::this_thread::interruption_point();
//	COutPoint key;
//        if (!pcursor->GetKey(key)) {
//            std::cout << __func__ << ": unable to read key" << std::endl;
//	    continue;
//        }
//        Coin coin;
//        if (pcursor->GetValue(coin)) {
////	    std::cout << "chain affinity of " << key.ToString() 
////		    << " = " << coin.chainAffinity 
////		    << std::endl;
//	    affinityCntMap[coin.shardAffinity]++;
//        } else {
//            std::cout << __func__ << ": unable to read coin" << std::endl;
//        }
//        pcursor->Next();
//    }
//    std::cout << "chain affinity stats : " << std::endl;
//    for(auto entry: affinityCntMap) {
//	std::cout << "affinity = " << entry.first << " UTXO count : "
//		<< entry.second << std::endl;
//    }
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

void randomPlaceTxInBlock(){
//    std::cout << "RANDOM place block " << randomPlaceBlock << std::endl;
//
//    CBlock block;
//    CBlockIndex* pblockindex = chainActive[randomPlaceBlock];
//    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
//	std::cerr << "Block not found on disk" << std::endl;
//    }
//    TxPlacer txPlacer(block);
//
//    /* start from the second transaction to exclude coinbase tx */
//    for (uint j = 1; j < block.vtx.size(); j++) {
//	txPlacer.randomPlaceTxid(block.vtx[j]);
//    }
//    txPlacer.printPlaceResult();
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

/* get the shard affinity for tx (i.e. output UTXOs) */
int32_t getShardAffinityForTx(CCoinsViewCache& cache, const CTransaction& tx) {
//	/* TODO: we currently use the shard affinity of the first tx input. Sort 
//	 * input shard affinity by frequence and use the most frequent one in the
//	 * future. */
//	int32_t shardAffinity = -1;
//	if (tx.IsCoinBase()) {
//	    /* evenly distribute coinbase output to all shards. */
//	    lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
//	    shardAffinity = lastAssignedAffinity;
//	} else {
//	    //const Coin firstInput = pcoinsTip->AccessCoin(tx.vin[0].prevout); 
//	    const Coin firstInput = cache.AccessCoin(tx.vin[0].prevout); 
//	    if (firstInput.IsSpent()) {
//		std::cout << __func__ << " : first input UTXO not found in coin cache." << std::endl;
//	    }
//	    shardAffinity = firstInput.shardAffinity;
//	}
//	return shardAffinity;
    return -1;
}