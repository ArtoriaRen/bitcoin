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

uint32_t blockStart;
uint32_t blockEnd;
uint32_t num_committees;

int TxPlacer::randomPlaceTxidIndex(CTransactionRef tx){
    return 0;
}

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
    return shardIds.size();
}

int TxPlacer::smartPlace(CTransactionRef tx){
    return 0;
}

void placeTxInBlocks(){
    TxPlacer txPlacer;
    std::map<uint, uint> shardCntMap; // key is shard count, value is tx count
    uint totalTxNum = 0;
    for (uint i = blockStart; i < blockEnd; i++){
	CBlockIndex* pblockindex = chainActive[i];
	totalTxNum += pblockindex->nTx - 1; // exclude the coinbase tx
	CBlock block;
	if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
	    // Block not found on disk. This could be because we have the block
	    // header in our index but don't have the block (for example if a
	    // non-whitelisted node sends us an unrequested long chain of valid
	    // blocks, we add the headers to our index, but don't accept the
	    // block).
	    std::cerr << "Block not found on disk" << std::endl;
	
	/* start from the second transaction to exclude coinbase tx */
	for (uint j = 1; j < block.vtx.size(); j++) {
//	    std::cout << txPlacer.randomPlaceTxid(block.vtx[j])
//		    << "-shard tx: " << block.vtx[j]->GetHash().GetHex()  << std::endl;

	    shardCntMap[txPlacer.randomPlaceTxid(block.vtx[j])]++;
	}
    }
    std::cout << "total tx num = " << totalTxNum << std::endl;
    std::cout << "tx shard num stats : " << std::endl;
    for(auto entry: shardCntMap) {
	std::cout << entry.first << "-shard tx count = "
		<< entry.second << std::endl;
    }
}