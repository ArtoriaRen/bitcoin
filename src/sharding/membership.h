/* 
 * File:   membership.h
 * Author: Liuyang Ren
 *
 * Created on March 26, 2019, 11:40 AM
 * 
 * Assign miners mining the last [106, 6] to groups using the nonce value of 
 * the sixth-latest block as a random number.
 */

#ifndef MEMBERSHIP_H
#define MEMBERSHIP_H

#include <validation.h>
#include <util.h>
#include <chain.h>
#include <chainparams.h>
#include <primitives/block.h>
#include <primitives/transaction.h>


class Shards
{
private:
    // Map group ids to the members in the group.
    std::unordered_map<int, std::vector<std::string>> mapGroupMember;

public:
    
    /**Specify the last block used to generate groups.
     */
    Shards(uint8_t groups, const CBlockIndex* pBlockIndex, const CChainParams& chainParams);
    ~Shards(){
    }
};


//ReadBlockFromDisk(block, pblockindex, Params().GetConsensus());
//GetTransaction( in validation.cpp

#endif /* MEMBERSHIP_H */

