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
#include <script/standard.h>
#include <pubkey.h>
#include <uint256.h>
#include <base58.h>

#include <memory>
#include <algorithm>


bool cmpTxOut(CTxOut txOut1, CTxOut txOut2 ){
  return txOut1.nValue < txOut2.nValue; 
}

Shards::Shards(const CBlockIndex* pblockindex, const CChainParams& chainParams) {
    // iterate the previous 100 blocks and assign their miners to groups.
    //        for(){
    LogPrintf("block height is: %d \n", pblockindex->nHeight);
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock& block = *pblock;
    ReadBlockFromDisk(block, pblockindex, chainParams.GetConsensus());
    CTransactionRef coinbaseTx = block.vtx[0];
    LogPrintf("Is coinbase tx : %d , hash= %s\n", coinbaseTx->IsCoinBase(), coinbaseTx->GetHash().ToString());
    CTxDestination address;
    std::vector<CTxOut>::const_iterator maxTxOut(std::max_element(coinbaseTx->vout.begin(), coinbaseTx->vout.end(), cmpTxOut)); 
    if (!ExtractDestination(maxTxOut->scriptPubKey, address)){
	LogPrintf("get address from scriptPubKey failed!");
    } else {
	LogPrintf("coinbase tx receiver account: %s\n", EncodeDestination(address));
    }
    
    //        }
}
