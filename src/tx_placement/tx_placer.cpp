/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <tx_placement/tx_placer.h>
#include <queue>
#include <fstream>
#include <string> 

#include "hash.h"
#include "arith_uint256.h"
#include "chain.h"
#include "validation.h"
#include "chainparams.h"
#include "txdb.h"
#include "rpc/server.h"
#include "core_io.h" // HexStr


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

    /* get output shard id */
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

std::vector<int32_t> TxPlacer::smartPlace(const CTransaction& tx, CCoinsViewCache& cache){
    /* random place coinbase tx */
    if (!tx.IsCoinBase()) { 
	return std::vector<int32_t>{randomPlaceUTXO(tx.GetHash())};
    }

    std::set<int> inputShardIds;
    /* Add the input shard ids to the set, and assign the outputShard with the 
     * shard id of the first input UTXO. 
     * Note: some input UTXOs might not be in the coinsViewCache, so we maintain
     * a map<txid, shardId> to keep track of the shard id of newly processed tx 
     * during test.
     */
    for(uint32_t i = 0; i < tx.vin.size(); i++) {
	if (cache.HaveCoin(tx.vin[i].prevout)) {
	    inputShardIds.insert(cache.AccessCoin(tx.vin[i].prevout).shardAffinity);
	} 
	else {
	    assert(mapTxShard.find(tx.vin[i].prevout.hash) != mapTxShard.end());
	    inputShardIds.insert(mapTxShard[tx.vin[i].prevout.hash]);
	}
    }

    /* get output shard id */
    int32_t outputShard = -1;
    if (cache.HaveCoin(tx.vin[0].prevout)) {
	outputShard = cache.AccessCoin(tx.vin[0].prevout).shardAffinity;
    }
    else {
	assert(mapTxShard.find(tx.vin[0].prevout.hash) != mapTxShard.end());
	outputShard = mapTxShard[tx.vin[0].prevout.hash];
    }
    
    /* Because this func is called by the 2PC coordinator, it should add the shard
     * info of this tx to the map for future use. 
     */
    mapTxShard.insert(std::make_pair(tx.GetHash(), outputShard));

    /* inputShardIds.size() is the shard span of this tx. */
    shardCntMap[tx.vin.size()][inputShardIds.size()]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret(inputShardIds.size() + 1);
    ret[0] = outputShard ;// put the outShardIt as the first element
    std::copy(inputShardIds.begin(), inputShardIds.end(), ret.begin() + 1);
    return ret;
}

/*
 * Strictly speaking, the return value should be either -1 or our shard id, but
 * as we use the entire chainstate at block 600999 in our test, we also have 
 * UTXOs not belonging to our shard in our coinsview.
 */
int32_t TxPlacer::smartPlaceUTXO(const COutPoint& txin, CCoinsViewCache& cache) {
    if (!cache.HaveCoin(txin)) {
	/* this is a coin generated during testing and not belong to our shard. */
	return -1;
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

void randomPlaceTxInBlock(){
    std::cout << "RANDOM place block " << randomPlaceBlock << std::endl;

    CBlock block;
    CBlockIndex* pblockindex = chainActive[randomPlaceBlock];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
	std::cerr << "Block not found on disk" << std::endl;
    }
    TxPlacer txPlacer;

    const int maxShardSpan = 16;
    std::ofstream txHexFiles[maxShardSpan];
    std::string fileNameSuffix("shardTx");
    for (int i = 0; i < maxShardSpan; i++) {
	txHexFiles[i].open(std::to_string(i + 1) + fileNameSuffix, std::ofstream::out | std::ofstream::trunc);
    }

    std::cout << "The block has " <<  block.vtx.size() -1 << " non-coinbase tx." << std::endl;
    /* start from the second transaction to exclude coinbase tx */
    for (uint j = 1; j < block.vtx.size(); j++) {
	std::vector<int32_t> shards = txPlacer.randomPlace(*block.vtx[j]);
	std::cout << "tx No. "  <<  j << " : ";
	for (int shard : shards) 
	    std::cout << shard << ", ";
	std::cout << std::endl;

	std::vector<int32_t>::iterator it = std::find(shards.begin() + 1, shards.end(), shards[0]);
	std::string hexstr = EncodeHexTx(*block.vtx[j], RPCSerializationFlags());
//	std::cout << hexstr << std::endl;
	if (it == shards.end()) {
	    /* the output shard is different than any input shards. */
	    txHexFiles[shards.size() - 1] << hexstr << std::endl;
	} else {
	    txHexFiles[shards.size() - 2] << hexstr << std::endl;
	}
    }
    for (int i = 0; i < maxShardSpan; i++) {
	txHexFiles[i].close();
    }
    std::cout << "Grouping tx finishes. " << randomPlaceBlock << std::endl;
}

void extractRawTxInBlock(){
    std::cout << "extract raw tx in block " << randomPlaceBlock << std::endl;

    CBlock block;
    CBlockIndex* pblockindex = chainActive[randomPlaceBlock];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
	std::cerr << "Block not found on disk" << std::endl;
    }

    std::ofstream txHexFile;
    txHexFile.open("tx_" + std::to_string(randomPlaceBlock) + ".out" , std::ofstream::out | std::ofstream::trunc);

    /* if we do not process coinbase tx, tx spending its output would fail. */
    std::cout << "The block has " <<  block.vtx.size() << " tx." << std::endl;
    for (uint j = 0; j < block.vtx.size(); j++) {
	std::string hexstr = EncodeHexTx(*block.vtx[j], RPCSerializationFlags());
	txHexFile << hexstr << std::endl;
    }
    txHexFile.close();
    std::cout << "extracting tx finishes. " << std::endl;
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
