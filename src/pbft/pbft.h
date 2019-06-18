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

//global view number

enum Phase {pre_prepare, prepare, commit, reply};

class CPbftMessage {
protected:
    Phase phase;
    uint32_t view;
    uint32_t seq;
    uint32_t sendeId;
    uint256 digest;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
};

class CPre_prepare : public CPbftMessage{
    CBlock block; 
    
public:
    CPre_prepare(){
    	phase = Phase::pre_prepare;
    }
};


class CPrepare: public CPbftMessage{

public:
    CPrepare(){
    	phase = Phase::prepare;
    }
};

class CCommit: public CPbftMessage{
    
public:
    CCommit(){
    	phase = Phase::commit;
    }
    
};

class CPbft{
public:
    int view;
    // TODO: the key type should be digest
    unordered_map<std::string, CPbftMessage> log;
    std::vector<CService> members;
    CService leader;
    uint32_t nGroups; // number of groups.
    
    CPbft(){
	view = 0;
        nGroups = 1;
    }
    
    // calculate the leader and group members based on the random number and the blockchain.
    void group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindex);
    
    CPre_prepare assemblePre_prepare(const CBlock& block);
    
    bool onReceivePrePrepare(const CPre_prepare& pre_prepare);
    
    void sendPrepare();
    
    //TODO: find the key used to sign and verify messages 
    void onReceivePrepare(const CPrepare& prepare);
	
 
    
    void sendCommit();
    
    
    
    
};


#endif /* PBFT_H */

