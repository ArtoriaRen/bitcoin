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
/* Only input shards will receive LOCK and  UNLOCK_TO_ABORT req;
 * Only output shards will receive UNLOCK_TO_COMMIT req.
 */
enum ClientReqType {TX, LOCK, UNLOCK_TO_COMMIT, UNLOCK_TO_ABORT};

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

    void getHash(uint256& result) const;
};

class CInputShardReply: public CReply {
public:
    /* total input amount in our shard. Must inform the output shard of this value.
     * Otherwise the output shard will have no enough info to verify if total input
     * value is larger than total output value. */
    CAmount totalValueInOfShard;
    CInputShardReply();
    CInputShardReply(char replyIn, const uint256& digestIn, const CAmount valueIn);

    template<typename Stream>
    void Serialize(Stream& s) const{
	CReply::Serialize(s);
	s.write((char*)&totalValueInOfShard, sizeof(totalValueInOfShard));
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	CReply::Unserialize(s);
	s.read((char*)&totalValueInOfShard, sizeof(totalValueInOfShard));
    }

    void getHash(uint256& result) const;
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
};

/* Although the LockReq class has the same member variable as the TxReq class,
 * a LockReq is executed differently: spent only input UTXOs in the shard and 
 * does not add any output UTXOs.
 */
class LockReq: public CClientReq {
public:
    CMutableTransaction tx_mutable;
    /* total input amount in our shard. Only in-memory. No need to serialize it. */
    mutable CAmount totalValueInOfShard;
    
    LockReq(): tx_mutable(CMutableTransaction()) {}
    LockReq(const CTransaction& txIn) : tx_mutable(txIn){}

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
};

class UnlockToCommitReq: public CClientReq {
public:
    CMutableTransaction tx_mutable;
    /* number of signatures from input shards. The output shard deems the first
     * three sigs are for the UTXOs appear first in the input list, ... 
     * TODO: figure out which publickey to use when verify the sigs.
     * Solution: 
     * - give every peer a identifier and assign them to shard base on 
     * their identifier. E.g. 0, 1,2,3 forms the first committee.   
     * - at start up, all peers of all groups connect to each other to exchange 
     * publickey as well as report their id so that every peer can build a <id, pubkey>
     * map. 
     */
    uint nInputShardReplies;  
    std::vector<CInputShardReply> vInputShardReply;

    UnlockToCommitReq();
    UnlockToCommitReq(const CTransaction& txIn, const uint sigCountIn, std::vector<CInputShardReply>&& vReply);

    template<typename Stream>
    void Serialize(Stream& s) const{
	tx_mutable.Serialize(s);
	s.write((char*)&nInputShardReplies, sizeof(nInputShardReplies));
	for (uint i = 0; i < nInputShardReplies; i++) {
	    vInputShardReply[i].Serialize(s);
	}
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
	tx_mutable.Unserialize(s);
	s.read((char*)&nInputShardReplies, sizeof(nInputShardReplies));
	vInputShardReply.resize(nInputShardReplies);
	for (uint i = 0; i < nInputShardReplies; i++) {
	    vInputShardReply[i].Unserialize(s);
	}
    }
    void Execute(const int seq) const override;
    uint256 GetDigest() const override;
};

/* We currently do not verify the proof-of-rejection part of OmniLeder.
 * proof-of-rejection verification requires every peer to know the publickey
 * of every committee. In our case of lacking threshold signature implementation,
 * the peer has to know all the pubkey keys of peers in the output committee and
 * verify everyone's signature. The process is tedious. */
//class UnlockToAbortReq: public CClientReq {
//public:
//    std::vector<COutPoint> voutpoint;
//
//    template<typename Stream>
//    void Serialize(Stream& s) const{
//	for (uint i = 0; i < voutpoint.size(); i++) {
//	    voutpoint[i].Serialize(s);
//	}
//    }
//    template<typename Stream>
//    void Unserialize(Stream& s) {
//	for (uint i = 0; i < voutpoint.size(); i++) {
//	    voutpoint[i].Unserialize(s);
//	}
//    }
//
//    /* mark UTXO(s) in the req as spent */
//    void Execute(const int seq) const override;
//    uint256 GetDigest() const override;
//};

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
	} else if (type == ClientReqType::LOCK) {
	    assert(req != nullptr);
	    static_cast<LockReq*>(req.get())->Serialize(s);
	} else if (type == ClientReqType::UNLOCK_TO_COMMIT) {
	    assert(req != nullptr);
	    static_cast<UnlockToCommitReq*>(req.get())->Serialize(s);
	}  
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	CPbftMessage::Unserialize(s);
	s.read((char*)&type, sizeof(type));
	if(type == ClientReqType::TX) {
	    req.reset(new TxReq());
	    static_cast<TxReq*>(req.get())->Unserialize(s);
	} else if(type == ClientReqType::LOCK) {
	    req.reset(new LockReq());
	    static_cast<LockReq*>(req.get())->Unserialize(s);
	} else if(type == ClientReqType::UNLOCK_TO_COMMIT) {
	    req.reset(new UnlockToCommitReq());
	    static_cast<UnlockToCommitReq*>(req.get())->Unserialize(s);
	}
    }
};





#endif /* PBFT_MSG_H */

