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

//typedef std::shared_ptr<CMutableTransaction> CMutableTxRef;

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
    std::vector<uint256> vTx; // a vector of tx hash
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CReply();
    CReply(char replyIn, std::deque<uint256>&& vTxIn);

    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write(&reply, sizeof(reply));
        uint32_t txCnt = vTx.size();
	s.write((char*)&txCnt, sizeof(txCnt));
	s.write((char*)vTx.data(), txCnt * vTx[0].size());
	s.write((char*)&peerID, sizeof(peerID));
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read((char*)&reply, sizeof(reply));
        uint32_t txCnt = 0;
	s.read(&txCnt, sizeof(txCnt));
        vTx.resize(txCnt);
	s.read((char*)vTx.data(), txCnt * vTx[0].size());
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
    std::vector<CTransactionRef> vReq;

    CPbftBlock();
    CPbftBlock(std::deque<CTransactionRef> vReqIn);
    void ComputeHash();
    uint32_t Execute(const int seq, CCoinsViewCache& view) const;
    uint32_t WarmupExecute(const int seq, CCoinsViewCache& view) const;
    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vReq);
    }

    //template<typename Stream>
    //void Serialize(Stream& s) const{
    //    uint block_size = vReq.size();
    //    s.write((char*)&block_size, sizeof(block_size));
    //    for (uint i = 0; i < vReq.size(); i++) {
    //        vReq[i]->Serialize(s);
    //    }
    //}

    //template<typename Stream>
    //void Unserialize(Stream& s) {
    //    uint block_size;
    //    s.read((char*)&block_size, sizeof(block_size));
    //    vReq.resize(block_size);
    //    for (uint i = 0; i < vReq.size(); i++) {
    //        vReq[i].reset(new CMutableTransaction);
    //        vReq[i]->Unserialize(s);
    //    }
    //}
};

class CBlockMsg {
public:
    uint32_t logSlot;
    std::shared_ptr<CPbftBlock> pPbftBlock;
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CBlockMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn, uint32_t seq);
    template<typename Stream>
    void Serialize(Stream& s) const {
        s.write((char*) &logSlot, sizeof (logSlot));
        pPbftBlock->Serialize(s);

        s.write((char*) &peerID, sizeof (peerID));
        s.write((char*) &sigSize, sizeof (sigSize));
        s.write((char*) vchSig.data(), sigSize);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        s.read((char*) &logSlot, sizeof (logSlot));
        pPbftBlock->Unserialize(s);

        s.read((char*) &peerID, sizeof (peerID));
        s.read((char*) &sigSize, sizeof (sigSize));
        vchSig.resize(sigSize);
        s.read((char*) vchSig.data(), sigSize);
    }

    void getHash(uint256& result) const;
};

#endif /* PBFT_MSG_H */

