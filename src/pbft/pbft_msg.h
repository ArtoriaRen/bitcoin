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

enum PbftPhase {pre_prepare, prepare, commit, reply};

class CPbftMessage {
public:
    PbftPhase phase;
    uint32_t view;
    uint32_t seq;
    uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    const static uint32_t messageSizeBytes = 128;

    CPbftMessage():phase(PbftPhase::pre_prepare), view(0), seq(0), digest(), vchSig(){
    }

    CPbftMessage(PbftPhase p):phase(p), view(0), seq(0), digest(), vchSig(){
    }

    // template function must be in header file.
    template<typename Stream>
    void serialize(Stream& s) const{
	 
	s << static_cast<int>(phase);
	s << view;
	s << seq;
	s << senderId;
	digest.Serialize(s);
	for(uint i = 0; i<vchSig.size(); i++){
	    s << vchSig[i];
	}
	
    }
    
    template<typename Stream>
    void deserialize(Stream& s) {
	int phaseInt;
	s >> phaseInt;
	std::cout << "phaseInt = "<< phaseInt << std::endl;
	
	phase = static_cast<PbftPhase>(phaseInt);
	s >> view;
	s >> seq;
	s >> senderId;
	digest.Unserialize(s); // 256 bits = 32 bytes
	char c;
	vchSig.clear();
	while(! (s >> c)){ // TODO: check the ret val when no more data.
	    std::cout << "deserialize for sig, c=" << c << std::endl;
	    vchSig.push_back(c);
	}
    }
};

class CPre_prepare : public CPbftMessage{
    // CBlock block;
    /* we can use P2P network to disseminate the block before the primary send Pre_prepare msg 
     * so that the block does not have to be in the Pre-prepare message.*/
    
public:
    CPre_prepare():CPbftMessage(PbftPhase::pre_prepare){
    }

    //add explicit?
    CPre_prepare(const CPbftMessage& msg){
	phase = PbftPhase::pre_prepare;
	view = msg.view;
	seq = msg.seq;
	senderId = msg.senderId;
	digest = msg.digest;
	vchSig = msg.vchSig;
    }
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

