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
