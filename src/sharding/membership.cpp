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

Shards::Shards(uint8_t groups, const CBlockIndex* pBlockIndex, const CChainParams& chainParams) {
    //    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock block;
    uint32_t randNum{block.nNonce};
    LogPrintf("last block nonce is (used as random number for sharding): %d \n", randNum);
    // iterate the previous 100 blocks and assign their miners to groups.
    for(int i=0; i<100 && pBlockIndex!= nullptr; i++){
	LogPrintf("block height is: %d \n", pBlockIndex->nHeight);
	ReadBlockFromDisk(block, pBlockIndex, chainParams.GetConsensus());
	CTransactionRef coinbaseTx = block.vtx[0];
	LogPrintf("Is coinbase tx : %d , hash= %s\n", coinbaseTx->IsCoinBase(), coinbaseTx->GetHash().GetHex());
	CTxDestination address;
	std::vector<CTxOut>::const_iterator maxTxOut(std::max_element(coinbaseTx->vout.begin(), coinbaseTx->vout.end(), cmpTxOut)); 
	if (!ExtractDestination(maxTxOut->scriptPubKey, address)){
	    LogPrintf("get address from scriptPubKey failed!");
	} else {
	    std::string strAddress =  EncodeDestination(address);
	    arith_uint256 hashRes = UintToArith256(singleHash(strAddress.begin(), strAddress.end(), randNum));
	    // compute the ramainder of divided by the value of groups.
	    arith_uint256 groupId = hashRes - (hashRes/groups) * groups;	
	    
	    LogPrintf("coinbase tx receiver account: %s, sha(address || nonce): %d, "
		    "assign to group: %d\n", 
		    strAddress, 
		    hashRes.GetHex(),
		    groupId.GetHex()
		    );
	}
	
	pBlockIndex = pBlockIndex->pprev; 
    }
}
