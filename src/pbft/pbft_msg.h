/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft_msg.h
 * Author: l27ren
 *
 * Created on July 8, 2019, 11:53 AM
 */

#ifndef PBFT_MSG_H
#define PBFT_MSG_H
#include "util.h"
#include "uint256.h"
//global view number

enum PbftPhase {pre_prepare, prepare, commit, execute, end};

class CPre_prepare;

class CPbftMessage {
public:
    PbftPhase phase;
    uint32_t view;
    uint32_t seq;
    uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    const static uint32_t messageSizeBytes = 128; // the real size is 4*4 + 32 +72 = 120 bytes.
    
    CPbftMessage();

    CPbftMessage(uint32_t senderId);
    
    CPbftMessage(PbftPhase p, uint32_t senderId);
    
    CPbftMessage(CPre_prepare& pre_prepare, uint32_t senderId);
    
    void serialize(std::ostringstream& s, const char* clientReq = nullptr) const;
    
    void deserialize(std::istringstream& s, char* clientReq = nullptr); 

    void getHash(uint256& result);
};

class CPre_prepare : public CPbftMessage{
    // CBlock block;
    /* we can use P2P network to disseminate the block before the primary send Pre_prepare msg 
     * so that the block does not have to be in the Pre-prepare message.*/
    
public:
    CPre_prepare():CPbftMessage(PbftPhase::pre_prepare){
    }
    
    //add explicit?
    CPre_prepare(const CPbftMessage& msg);

    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 

    std::string clientReq;
};


class CPrepare: public CPbftMessage{
    
public:
    CPrepare():CPbftMessage(PbftPhase::prepare){
    }
};

class CCommit: public CPbftMessage{
    
public:
    CCommit():CPbftMessage(PbftPhase::commit){
    }
    
};

#endif /* PBFT_MSG_H */

