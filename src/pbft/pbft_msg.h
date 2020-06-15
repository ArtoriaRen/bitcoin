/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft_msg.h
 * Author: l27ren
 *
 * Created on June 11, 2020, 11:41 AM
 */

#ifndef PBFT_MSG_H
#define PBFT_MSG_H

#include "util.h"
#include "uint256.h"
#include "primitives/transaction.h"
//global view number

enum PbftPhase {pre_prepare, prepare, commit, reply, end};

class CPre_prepare;

class CPbftMessage {
public:
    //PbftPhase phase;
    uint32_t view;
    uint32_t seq;
    //uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
//    const static uint32_t messageSizeBytes = 128; // the real size is 4*4 + 32 +72 = 120 bytes.
    
    CPbftMessage();
    CPbftMessage(const CPbftMessage& msg);
    
    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write((char*)&view, sizeof(view));
	s.write((char*)&seq, sizeof(seq));
	std::cout<< __func__ << ": digest = " << digest.GetHex() <<std::endl;
	s.write((char*)digest.begin(), digest.size());
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read((char*)&view, sizeof(view));
	s.read((char*)&seq, sizeof(seq));
	s.read((char*)digest.begin(), digest.size());
	std::cout<< __func__ << ": digest = " << digest.GetHex() <<std::endl;
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }

    void getHash(uint256& result);
};

class CPre_prepare : public CPbftMessage{
    // CBlock block;
    /* we can use P2P network to disseminate the block before the primary send Pre_prepare msg 
     * so that the block does not have to be in the Pre-prepare message.*/
    
public:
    CPre_prepare():CPbftMessage(){
    }
    
    //add explicit?
    CPre_prepare(const CPre_prepare& msg);
    CPre_prepare(const CPbftMessage& msg);

    template<typename Stream>
    void Serialize(Stream& s) const{
	CPbftMessage::Serialize(s);
	tx.Serialize(s);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	CPbftMessage::Unserialize(s);
	tx.Unserialize(s);
    }

    CMutableTransaction tx;
};


//class CPrepare: public CPbftMessage{
//    
//public:
//    CPrepare():CPbftMessage(PbftPhase::prepare){
//    }
//};
//
//class CCommit: public CPbftMessage{
//    
//public:
//    CCommit():CPbftMessage(PbftPhase::commit){
//    }
//    
//};


/*Local pre-prepare message*/
class CReply {
public:
    PbftPhase phase;
    uint32_t seq;
    uint32_t senderId;
    char reply; // execution result
    std::string timestamp;
    uint256 digest; // use the block header hash as digest.
    /* TODO: change the YCSB workload (probably hash each key and value to constant size)
     * so that the reply has a fixed size.
     * Assume the reply is 1 byte for now.
     */
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    // the real size of a reply msg is 4*3 + 1 + 32 + 72 = 117 bytes.

    CReply();
    CReply(uint32_t seqNum, const uint32_t sender, char rpl, const uint256& dgt, std::string timestamp);

    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 

    void getHash(uint256& result);
};


#endif /* PBFT_MSG_H */

