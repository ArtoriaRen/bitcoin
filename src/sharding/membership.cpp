/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <sharding/membership.h>
#include <sharding/sha256.h>
#include <script/standard.h>
#include <pubkey.h>
#include <uint256.h>
#include <base58.h>

#include <memory>
#include <algorithm>


bool cmpTxOut(CTxOut txOut1, CTxOut txOut2 ){
    return txOut1.nValue < txOut2.nValue; 
}

Shards::Shards(uint8_t groups, const CBlockIndex* pBlockIndex, const CChainParams& chainParams){
    assert(mapGroupMember.size() == 0);
    CBlock block;
    uint32_t randNum{block.nNonce};
    LogPrintf("last block nonce is (used as random number for sharding): %d \n", randNum);
    
    // iterate the previous 100 blocks and assign their miners to groups.
    for(int i=0; i<100 && pBlockIndex!= nullptr; i++){
	ReadBlockFromDisk(block, pBlockIndex, chainParams.GetConsensus());
	CTransactionRef coinbaseTx = block.vtx[0];
	LogPrintf("block height is: %d,  first tx hash= %s, is coinbase tx : %d \n", pBlockIndex->nHeight, coinbaseTx->GetHash().GetHex(), coinbaseTx->IsCoinBase());
	CTxDestination address;
	std::vector<CTxOut>::const_iterator maxTxOut(std::max_element(coinbaseTx->vout.begin(), coinbaseTx->vout.end(), cmpTxOut)); 
	if (!ExtractDestination(maxTxOut->scriptPubKey, address)){
	    LogPrintf("get address from scriptPubKey failed!");
	} else {
	    // coinbase tx receiver account, aka the miner of this block.
	    std::string strAddress =  EncodeDestination(address);
	    // hash miner's account concatenated with the random number.
	    arith_uint256 hashRes = UintToArith256(singleHash(strAddress.begin(), strAddress.end(), randNum));
	    // compute the remainder of divided by the value of groups.
	    uint32_t groupId = static_cast<uint32_t>((hashRes - (hashRes/groups) * groups).GetLow64());
	    mapGroupMember[groupId].push_back(strAddress);
	    LogPrintf("add miner %s to group %d\n", mapGroupMember[groupId].back(), groupId);
	}
	pBlockIndex = pBlockIndex->pprev; 
    }
}

void Shards::getGroups(){

	   
}
