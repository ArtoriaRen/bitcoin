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
enum ClientReqType {TX, LOCK, UNLOCK};

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
	s.write((char*)digest.begin(), digest.size());
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read((char*)&view, sizeof(view));
	s.read((char*)&seq, sizeof(seq));
	s.read((char*)digest.begin(), digest.size());
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }

    void getHash(uint256& result);
};

class CClientReq{
public:
    /* we did not put serialization methods here because c++ does not allow
     * virtual template method.
     */
    virtual void Execute(const int seq) const = 0; // seq is passed in because we use it as block height.
    virtual uint256 GetDigest() const = 0;
//    virtual ~CClientReq(){};
};

class TxReq: public CClientReq {
public:
    CMutableTransaction tx_mutable;
    
    TxReq(): tx_mutable(CMutableTransaction()) {}
    TxReq(const CTransaction& txIn) : tx_mutable(txIn){}

    template<typename Stream>
    void Serialize(Stream& s) const{
	tx_mutable.Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
	tx_mutable.Unserialize(s);
    }
    void Execute(const int seq) const override;
    uint256 GetDigest() const override;

//    ~TxReq(){
//	~tx();
//    }
};

class CPre_prepare : public CPbftMessage{
public:
    /* The client request type (currently only for OmniLedger):
     * The type is used to decide how the client request should be serialized,
     * and deserialized when a peer receives a ppMsg.
     */
    ClientReqType type;
    /* If we use P2P network to disseminate client req before the primary send Pre_prepare msg,
     * the req does not have to be in the Pre-prepare message.*/
    std::shared_ptr<CClientReq> req;

   
    CPre_prepare():CPbftMessage(), req(nullptr){ }
    CPre_prepare(const CPbftMessage& pbftMsg, const std::shared_ptr<CClientReq>& reqIn):CPbftMessage(pbftMsg), req(reqIn){ }
    
    //add explicit?
    CPre_prepare(const CPre_prepare& msg);
    CPre_prepare(const CPbftMessage& msg);

    template<typename Stream>
    void Serialize(Stream& s) const{
	CPbftMessage::Serialize(s);
	s.write((char*)&type, sizeof(type));
	if(type == ClientReqType::TX) {
	    assert(req != nullptr);
	    static_cast<TxReq*>(req.get())->Serialize(s);
	}
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	CPbftMessage::Unserialize(s);
	s.read((char*)&type, sizeof(type));
	if(type == ClientReqType::TX) {
	    req.reset(new TxReq());
	    static_cast<TxReq*>(req.get())->Unserialize(s);
	}
    }

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
    char reply; // execution result
    uint256 digest; // use the tx header hash as digest.
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CReply();
    CReply(char replyIn, const uint256& digestIn);

    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write(&reply, sizeof(reply));
	s.write((char*)digest.begin(), digest.size());
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read(&reply, sizeof(reply));
	s.read((char*)digest.begin(), digest.size());
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }

    void getHash(uint256& result);
};


#endif /* PBFT_MSG_H */

