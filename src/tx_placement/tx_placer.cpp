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
#include "txdb.h"

/* global variable configurable from conf file. */
uint32_t blockStart;
uint32_t blockEnd;
uint32_t num_committees;

static int lastAssignedAffinity = -1;

//int TxPlacer::randomPlaceTxidIndex(CTransactionRef tx){
//    return 0;
//}

/* all output UTXOs of a tx is stored in one shard. */
int TxPlacer::randomPlaceTxid(CTransactionRef tx){
    std::set<int> shardIds;
    /* add the output shard id to the above set */
    arith_uint256 txid = UintToArith256(tx->GetHash());
    arith_uint256 quotient = txid / num_committees;
    arith_uint256 outShardId = txid - quotient * num_committees;
    shardIds.insert((int)(outShardId.GetLow64()));
    
    /* add the input shard ids to the set */
    for(uint32_t i = 0; i < tx->vin.size(); i++) {
	arith_uint256 txid = UintToArith256(tx->vin[i].prevout.hash);
	arith_uint256 quotient = txid / num_committees;
	arith_uint256 inShardId = txid - quotient * num_committees;
	shardIds.insert((int)(inShardId.GetLow64()));
    }

//    std::cout << "tx " << tx->GetHash().GetHex() << " spans shards : ";
//    for(auto entry : shardIds) {
//	std::cout << entry << " ";
//    }
//    std::cout << std::endl;
    
    return shardIds.size();
}

int TxPlacer::smartPlace(CTransactionRef tx){
    std::set<int> shardIds;
    const Coin& firstUtxoIn = pcoinsTip->AccessCoin(tx->vin[0].prevout);
    /* if the coin is not found, an empty coin with null CTxOut is returned. */
    std::cout << "found " << tx->vin[0].prevout.ToString() << " = " << !firstUtxoIn.IsSpent() << std::endl;

    /* add output shard id */
    if(firstUtxoIn.shardAffinity != -1) {
	shardIds.insert(firstUtxoIn.shardAffinity);
    } else {
	/* the first utxo has not been assigned chainAffinity. Assign it now, and
	 * the all output UTXOs should be with the same chainAffinity. */
	lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
	shardIds.insert(lastAssignedAffinity);
    }
    /* add input shard id. (The shard of the first input UXTO has already been 
     * considered when adding output shard id, so here we start with the second
     * input UTXO.)*/
    for(uint32_t i = 1; i < tx->vin.size(); i++) {
	Coin utxoIn;
	pcoinsTip->GetCoin(tx->vin[i].prevout, utxoIn);
	if(utxoIn.shardAffinity != -1) {
	    shardIds.insert(utxoIn.shardAffinity);
	} else {
	    /* the first utxo has not been assigned chainAffinity. Assign it now, and
	     * the all output UTXOs should be with the same chainAffinity. */
	    lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
	    shardIds.insert(lastAssignedAffinity);
	}
    }
    return shardIds.size();
}

void assignShardAffinity(){
    std::cout << "Assigning shard affinity for all UTXOs..." << std::endl;
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
	    lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
	    coin.shardAffinity = lastAssignedAffinity;
	    pcoinsTip->AddCoin(key, std::move(coin), true);
//	    std::cout << "chain affinity of " << key.ToString() 
//		    << " = " << coin.chainAffinity 
//		    << std::endl;
        } else {
            std::cout << __func__ << ": unable to read coin" << std::endl;
        }
        pcursor->Next();
    }
    pcoinsTip->Flush();
}

void printChainAffinity(){
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
	std::cout << "affinity = " << entry.first << " count : "
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

void randomPlaceTxInBlock(const uint32_t block_height){
    std::cout << "RANDOM place block " << block_height;
    TxPlacer txPlacer;
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum = 0;

    CBlock block;
    CBlockIndex* pblockindex = chainActive[block_height];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
	std::cerr << "Block not found on disk" << std::endl;
    }
    totalTxNum += block.vtx.size() - 1; // exclude the coinbase tx
    /* start from the second transaction to exclude coinbase tx */
    for (uint j = 1; j < block.vtx.size(); j++) {
	shardCntMap[block.vtx[j]->vin.size()][txPlacer.randomPlaceTxid(block.vtx[j])]++;
    }
    std::cout << ", total tx num (excluding coinbase) = " << totalTxNum;
    uint32_t nMoreThan4InputSingleShard = 0, nMoreThan4InputCrossShard = 0;
    for(auto entry: shardCntMap) {
        /* count single-shard tx*/
        if (entry.first <= 4) {
            std::cout << "; " << entry.first << "-input_UTXO tx" << ": single-shard tx count = " << entry.second[1];
        } else {
            nMoreThan4InputSingleShard += entry.second[1];
        }

        /* count cross-shard tx*/
        uint32_t nCrossShardTx = 0;
	for (auto p : entry.second) {
            nCrossShardTx += p.first == 1 ? 0 : p.second;
	}
        if (entry.first <= 4) {
            std::cout << ", cross-shard tx count = " << nCrossShardTx;
        } else {
            nMoreThan4InputCrossShard += nCrossShardTx;
        }
    }
    std::cout << "; more-input_UTXO tx" << ": single-shard tx count = " << nMoreThan4InputSingleShard << ", cross-shard tx count = " << nMoreThan4InputCrossShard << std::endl;

}

void smartPlaceTxInBlock(const std::shared_ptr<const CBlock> pblock){
    std::cout << "SMART place block " << chainActive.Height() + 1 << std::endl;
    TxPlacer txPlacer;
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum = 0;
    totalTxNum += pblock->vtx.size() - 1; // exclude the coinbase tx
    /* start from the second transaction to exclude coinbase tx */
    for (uint j = 1; j < pblock->vtx.size(); j++) {
	shardCntMap[pblock->vtx[j]->vin.size()][txPlacer.smartPlace(pblock->vtx[j])]++;
    }
    std::cout << "total tx num = " << totalTxNum << std::endl;
    std::cout << "tx shard num stats : " << std::endl;
    for(auto entry: shardCntMap) {
	std::cout << "\t" <<  entry.first << "-input_UTXO tx: " << std::endl;
	for (auto p : entry.second) {
	    std::cout << "\t\t" << p.first << "-shard tx count = " << p.second << std::endl;
	}
    }
}

/* get the shard affinity for tx (i.e. output UTXOs) */
int32_t getShardAffinityForTx(const CTransaction& tx) {
	/* TODO: we currently use the shard affinity of the first tx input. Sort 
	 * input shard affinity by frequence and use the most frequent one in the
	 * future. */
	int32_t shardAffinity = -1;
	if (tx.IsCoinBase()) {
	    /* evenly distribute coinbase output to all shards. */
	    lastAssignedAffinity = (lastAssignedAffinity + 1) % num_committees;
	    shardAffinity = lastAssignedAffinity;
	} else {
	    const Coin firstInput = pcoinsTip->AccessCoin(tx.vin[0].prevout); 
	    if (firstInput.IsSpent()) {
		std::cout << __func__ << " : first input UTXO not found in coin cache." << std::endl;
	    }
	    shardAffinity = firstInput.shardAffinity;
	}
	return shardAffinity;
}

