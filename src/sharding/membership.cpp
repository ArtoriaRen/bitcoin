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

Shards::Shards(const CBlockIndex* pblockindex, const CChainParams& chainParams) {
    // iterate the previous 100 blocks and assign their miners to groups.
    //        for(){
    LogPrintf("block height is: %d \n", pblockindex->nHeight);
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock& block = *pblock;
    ReadBlockFromDisk(block, pblockindex, chainParams.GetConsensus());
    LogPrintf("Is coinbase tx : %d , hash= %s, hex_hash = %x\n", (block.vtx[0])->IsCoinBase(), (block.vtx[0])->GetHash().ToString(), (block.vtx[0])->GetHash().GetHex());

    //        }
}
