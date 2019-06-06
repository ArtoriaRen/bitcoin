/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft.h
 * Author: liuyangren
 *
 * Created on June 4, 2019, 2:37 PM
 */

#ifndef PBFT_H
#define PBFT_H
#include "netaddress.h"
#include "util.h"
#include "primitives/block.h"
#include "chain.h"

enum Phase {pre_prepare, prepare, commit, reply};
       
struct CPbftMessage {
    std::string Phase;
    uint32_t view;
    uint32_t seq;
    uint32_t sendeId;
    //what type?  digest;
};

struct CPre_prepare : public CPbftMessage{
    CBlock block; 
};

class CPbft{
public:
    std::vector<CService> members;
    CService leader;
 
    uint32_t nGroups; // number of groups.
    
    CPbft(){
        nGroups = 1;
    }
    
    // calculate the leader and group members based on the random number and the blockchain.
    void group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindex);
    
    
};


#endif /* PBFT_H */

