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
#include "net.h"
#include "coins.h"

typedef std::shared_ptr<CMutableTransaction> CMutableTxRef;

enum PbftPhase {pre_prepare, prepare, commit, reply, end};

class CPbftMessage {
public:
    //PbftPhase phase;
    uint32_t view;
    uint32_t seq;
    //uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
//    const static uint32_t messageSizeBytes = 128; // the real size is 4*4 + 32 +72 = 120 bytes.
    
    CPbftMessage();
    CPbftMessage(const CPbftMessage& msg);
    
    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write((char*)&view, sizeof(view));
	s.write((char*)&seq, sizeof(seq));
	s.write((char*)digest.begin(), digest.size());
	s.write((char*)&peerID, sizeof(peerID));
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read((char*)&view, sizeof(view));
	s.read((char*)&seq, sizeof(seq));
	s.read((char*)digest.begin(), digest.size());
	s.read((char*)&peerID, sizeof(peerID));
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }

    void getHash(uint256& result);
};

class CReply {
public:
    char reply; // execution result
    uint256 digest; // use the tx header hash as digest.
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CReply();
    CReply(char replyIn, const uint256& digestIn);

    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write(&reply, sizeof(reply));
	s.write((char*)digest.begin(), digest.size());
	s.write((char*)&peerID, sizeof(peerID));
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read(&reply, sizeof(reply));
	s.read((char*)digest.begin(), digest.size());
	s.read((char*)&peerID, sizeof(peerID));
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }

    void getHash(uint256& result) const;
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



class CPbftBlock{
public:
    uint256 hash; 
    std::vector<CMutableTxRef> vReq;

    CPbftBlock();
    CPbftBlock(std::deque<CMutableTxRef> vReqIn);
    void ComputeHash();
    uint32_t Execute(const int seq, CConnman* connman) const;

    template<typename Stream>
    void Serialize(Stream& s) const{
	uint block_size = vReq.size();
	s.write((char*)&block_size, sizeof(block_size));
	for (uint i = 0; i < vReq.size(); i++) {
	    vReq[i]->Serialize(s);
	}
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
	uint block_size;
	s.read((char*)&block_size, sizeof(block_size));
	vReq.resize(block_size);
	for (uint i = 0; i < vReq.size(); i++) {
	    vReq[i].reset(new CMutableTransaction);
	    vReq[i]->Unserialize(s);
	}
    }
};

class CPre_prepare : public CPbftMessage{
public:
    CPbftBlock pbft_block;
   
    CPre_prepare():CPbftMessage(), pbft_block() { }
    CPre_prepare(const CPbftMessage& pbftMsg, const CPbftBlock& blockIn):CPbftMessage(pbftMsg), pbft_block(blockIn) { }
    
    CPre_prepare(const CPre_prepare& msg);
    CPre_prepare(const CPbftMessage& msg);

    template<typename Stream>
    void Serialize(Stream& s) const{
	CPbftMessage::Serialize(s);
	pbft_block.Serialize(s);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	CPbftMessage::Unserialize(s);
	pbft_block.Unserialize(s);
    }
};

#endif /* PBFT_MSG_H */

