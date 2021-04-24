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
//global view number

enum PbftPhase {pre_prepare, prepare, commit, reply, end};
/* Only input shards will receive LOCK and  UNLOCK_TO_ABORT req;
 * Only output shards will receive UNLOCK_TO_COMMIT req.
 */
enum ClientReqType {TX = 0, LOCK, UNLOCK_TO_COMMIT, UNLOCK_TO_ABORT};

class TxIndexOnChain {
public:
    uint32_t block_height;
    uint32_t offset_in_block;

    TxIndexOnChain();
    TxIndexOnChain(const uint32_t block_height_in, const uint32_t offset_in_block_in);
    bool IsNull();

    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write(reinterpret_cast<const char*>(&block_height), sizeof(block_height));
	s.write(reinterpret_cast<const char*>(&offset_in_block), sizeof(offset_in_block));
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read(reinterpret_cast<char*>(&block_height), sizeof(block_height));
	s.read(reinterpret_cast<char*>(&offset_in_block), sizeof(offset_in_block));
    }

    TxIndexOnChain operator+(const unsigned int oprand);

    friend bool operator<(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator>(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator==(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator!=(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator<=(const TxIndexOnChain& a, const TxIndexOnChain& b);

    std::string ToString() const;
};

class CPbft;

class CPre_prepare;

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

/*Local pre-prepare message*/
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
    ClientReqType type;
    CTransactionRef pTx; 

    CClientReq();
    CClientReq(const CTransactionRef pTxIn);

    /* we did not put serialization methods here because c++ does not allow
     * virtual template method.
     */
    virtual bool Verify(const int seq, CCoinsViewCache& view) const = 0; // seq is passed in because we use it as block height.
    virtual bool Execute(const int seq, CCoinsViewCache& view) const = 0; // seq is passed in because we use it as block height.
    virtual uint256 GetDigest() const = 0;

//    virtual ~CClientReq(){};
};

class TxReq: public CClientReq {
public:
    TxReq();
    TxReq(const CTransactionRef pTxIn);

    template<typename Stream>
    void Serialize(Stream& s) const{
        s.write((char*) &(type), sizeof(type));
        pTx->Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ClientReqType::TX;
        //CMutableTransaction tx_mut;
        //tx_mut.Unserialize(s);
        //pTx = std::make_shared<const CTransaction>(deserialize, s);
        s >> pTx;
    }
    bool Verify(const int seq, CCoinsViewCache& view) const override;
    bool Execute(const int seq, CCoinsViewCache& view) const override;
    uint256 GetDigest() const override;
};

/* Although the LockReq class has the same member variable as the TxReq class,
 * a LockReq is executed differently: spent only input UTXOs in the shard and 
 * does not add any output UTXOs.
 */
class LockReq: public CClientReq {
public:
    /* total input amount in our shard. Only in-memory. No need to serialize it. */
    mutable CAmount totalValueInOfShard;
    
    LockReq();
    LockReq(const CTransactionRef pTxIn);

    template<typename Stream>
    void Serialize(Stream& s) const{
        s.write((char*) &(type), sizeof(type));
        pTx->Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ClientReqType::LOCK;
        s >> pTx;
    }
    bool Verify(const int seq, CCoinsViewCache& view) const override;
    bool Execute(const int seq, CCoinsViewCache& view) const override;
    uint256 GetDigest() const override;
};

class UnlockToCommitReq: public CClientReq {
public:
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
    UnlockToCommitReq(const CTransactionRef pTxIn, const uint sigCountIn, std::vector<CInputShardReply>&& vReply);

    template<typename Stream>
    void Serialize(Stream& s) const{
        s.write((char*) &(type), sizeof(type));
        pTx->Serialize(s);
	s.write((char*)&nInputShardReplies, sizeof(nInputShardReplies));
	for (uint i = 0; i < nInputShardReplies; i++) {
	    vInputShardReply[i].Serialize(s);
	}
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ClientReqType::UNLOCK_TO_COMMIT;
        s >> pTx;
	s.read((char*)&nInputShardReplies, sizeof(nInputShardReplies));
	vInputShardReply.resize(nInputShardReplies);
	for (uint i = 0; i < nInputShardReplies; i++) {
	    vInputShardReply[i].Unserialize(s);
	}
    }
    bool Verify(const int seq, CCoinsViewCache& view) const override;
    bool Execute(const int seq, CCoinsViewCache& view) const override;
    uint256 GetDigest() const override;
};

class UnlockToAbortReq: public CClientReq {
public:
    /* We need only lock-fail replies from only one input shard to abort the tx,
     * i.e. 2f + 1 negative replies from the same shard. 
     */
    std::vector<CInputShardReply> vNegativeReply;

    UnlockToAbortReq();
    UnlockToAbortReq(const CTransactionRef pTxIn, const std::vector<CInputShardReply>& lockFailReplies);

    template<typename Stream>
    void Serialize(Stream& s) const{
        s.write((char*) &(type), sizeof(type));
        pTx->Serialize(s);
	for (auto reply: vNegativeReply) {
	    reply.Serialize(s);
	}
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ClientReqType::UNLOCK_TO_ABORT;
        s >> pTx;
	for (uint i = 0; i < vNegativeReply.size(); i++) {
	     vNegativeReply[i].Unserialize(s);
	}
    }
    bool Verify(const int seq, CCoinsViewCache& view) const override;
    bool Execute(const int seq, CCoinsViewCache& view) const override;
    uint256 GetDigest() const override;
};

class CPbftBlock{
public:
    uint256 hash; 
    std::vector<std::shared_ptr<CClientReq>> vPReq;

    CPbftBlock();
    CPbftBlock(std::deque<std::shared_ptr<CClientReq>>&& vPReqIn);
    void ComputeHash();
    void WarmUpExecute(const int seq, CCoinsViewCache& view) const;
    /* verify and execute prereq-clear tx in this block. */
    uint32_t Verify(const int seq, CCoinsViewCache& view, std::vector<char>& validTxs, std::vector<uint32_t>& invalidTxs) const;
    uint32_t Execute(const int seq, CConnman* connman, CCoinsViewCache& view) const;
    void Clear();

    template<typename Stream>
    void Serialize(Stream& s) const{
	uint block_size = vPReq.size();
	s.write((char*)&block_size, sizeof(block_size));
	for (uint i = 0; i < vPReq.size(); i++) {
            ClientReqType type = vPReq[i]->type;
            if (type == ClientReqType::TX) {
                assert(vPReq[i] != nullptr);
                static_cast<TxReq*>(vPReq[i].get())->Serialize(s);
            } else if (type == ClientReqType::LOCK) {
                assert(vPReq[i] != nullptr);
                static_cast<LockReq*>(vPReq[i].get())->Serialize(s);
            } else if (type == ClientReqType::UNLOCK_TO_COMMIT) {
                assert(vPReq[i] != nullptr);
                static_cast<UnlockToCommitReq*>(vPReq[i].get())->Serialize(s);
            } else if (type == ClientReqType::UNLOCK_TO_ABORT) {
                assert(vPReq[i] != nullptr);
                static_cast<UnlockToAbortReq*>(vPReq[i].get())->Serialize(s);
            }  
	}
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
	uint block_size;
	s.read((char*)&block_size, sizeof(block_size));
	vPReq.resize(block_size);
        ClientReqType type;
	for (uint i = 0; i < vPReq.size(); i++) {
            s.read((char*)&type, sizeof(type));
            if(type == ClientReqType::TX) {
                vPReq[i].reset(new TxReq());
                static_cast<TxReq*>(vPReq[i].get())->Unserialize(s);
            } else if(type == ClientReqType::LOCK) {
                vPReq[i].reset(new LockReq());
                static_cast<LockReq*>(vPReq[i].get())->Unserialize(s);
            } else if(type == ClientReqType::UNLOCK_TO_COMMIT) {
                vPReq[i].reset(new UnlockToCommitReq());
                static_cast<UnlockToCommitReq*>(vPReq[i].get())->Unserialize(s);
            } else if(type == ClientReqType::UNLOCK_TO_ABORT) {
                vPReq[i].reset(new UnlockToAbortReq());
                static_cast<UnlockToAbortReq*>(vPReq[i].get())->Unserialize(s);
            }
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

class CReqBatch {
public:
    std::deque<std::shared_ptr<CClientReq>> vPReq;

    CReqBatch();

    template<typename Stream>
    void Serialize(Stream& s) const {
        uint batch_size = vPReq.size();
        s.write((char*) &batch_size, sizeof (batch_size));
        for (uint i = 0; i < vPReq.size(); i++) {
            ClientReqType type = vPReq[i]->type;
            if (type == ClientReqType::TX) {
                assert(vPReq[i] != nullptr);
                static_cast<TxReq*>(vPReq[i].get())->Serialize(s);
            } else if (type == ClientReqType::LOCK) {
                assert(vPReq[i] != nullptr);
                static_cast<LockReq*>(vPReq[i].get())->Serialize(s);
            } else if (type == ClientReqType::UNLOCK_TO_COMMIT) {
                assert(vPReq[i] != nullptr);
                static_cast<UnlockToCommitReq*>(vPReq[i].get())->Serialize(s);
            } else if (type == ClientReqType::UNLOCK_TO_ABORT) {
                assert(vPReq[i] != nullptr);
                static_cast<UnlockToAbortReq*>(vPReq[i].get())->Serialize(s);
            }  
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        uint batch_size;
        s.read((char*) &batch_size, sizeof (batch_size));
        vPReq.resize(batch_size);
        ClientReqType type;
	for (uint i = 0; i < vPReq.size(); i++) {
            s.read((char*)&type, sizeof(type));
            if(type == ClientReqType::TX) {
                vPReq[i].reset(new TxReq());
                static_cast<TxReq*>(vPReq[i].get())->Unserialize(s);
            } else if(type == ClientReqType::LOCK) {
                vPReq[i].reset(new LockReq());
                static_cast<LockReq*>(vPReq[i].get())->Unserialize(s);
            } else if(type == ClientReqType::UNLOCK_TO_COMMIT) {
                vPReq[i].reset(new UnlockToCommitReq());
                static_cast<UnlockToCommitReq*>(vPReq[i].get())->Unserialize(s);
            } else if(type == ClientReqType::UNLOCK_TO_ABORT) {
                vPReq[i].reset(new UnlockToAbortReq());
                static_cast<UnlockToAbortReq*>(vPReq[i].get())->Unserialize(s);
            }
        }
    }
};

class CCollabMessage {
public:
    uint32_t height;
    uint32_t txCnt;
    std::vector<char> validTxs;
    std::vector<uint32_t> invalidTxs;
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CCollabMessage();
    CCollabMessage(uint32_t heightIn, uint32_t txCntIn, std::vector<char>&& validTxsIn, std::vector<uint32_t>&& invalidTxsIn);
    
    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write((char*)&height, sizeof(height));
	s.write((char*)&txCnt, sizeof(txCnt));
        uint32_t vector_size = validTxs.size();
	s.write((char*)&vector_size, sizeof(vector_size));
	s.write((char*)validTxs.data(), vector_size);
        vector_size = invalidTxs.size();
	s.write((char*)&vector_size, sizeof(vector_size));
	s.write((char*)invalidTxs.data(), vector_size * sizeof(uint32_t));

	s.write((char*)&peerID, sizeof(peerID));
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read((char*)&height, sizeof(height));
	s.read((char*)&txCnt, sizeof(txCnt));
        uint32_t vector_size = 0;
        s.read((char*)&vector_size, sizeof(vector_size));
	validTxs.resize(vector_size);
	s.read((char*)validTxs.data(), vector_size);
        s.read((char*)&vector_size, sizeof(vector_size));
	invalidTxs.resize(vector_size);
	s.read((char*)invalidTxs.data(), vector_size * sizeof(uint32_t));

	s.read((char*)&peerID, sizeof(peerID));
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }
    void getHash(uint256& result) const;

    /* fetch the bit for this tx. If the bit is 1, then the tx is valid. */
    bool isValidTx(const uint32_t txSeq) const;
};

class CCollabMultiBlockMsg {
public:
    std::vector<TxIndexOnChain> validTxs;
    std::vector<TxIndexOnChain> invalidTxs;
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CCollabMultiBlockMsg();
    CCollabMultiBlockMsg(std::vector<TxIndexOnChain>&& validTxs, std::vector<TxIndexOnChain>&& invalidTxs);
    
    template<typename Stream>
    void Serialize(Stream& s) const{
        uint32_t vector_size = validTxs.size();
	s.write((char*)&vector_size, sizeof(vector_size));
        for (uint i = 0; i < vector_size; i++) {
            validTxs[i].Serialize(s);
        }
        vector_size = invalidTxs.size();
	s.write((char*)&vector_size, sizeof(vector_size));
        for (uint i = 0; i < vector_size; i++) {
            invalidTxs[i].Serialize(s);
        }

	s.write((char*)&peerID, sizeof(peerID));
	s.write((char*)&sigSize, sizeof(sigSize));
	s.write((char*)vchSig.data(), sigSize);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
        uint32_t vector_size = 0;
        s.read((char*)&vector_size, sizeof(vector_size));
	validTxs.resize(vector_size);
        for (uint i = 0; i < vector_size; i++) {
            validTxs[i].Unserialize(s);
        }
        s.read((char*)&vector_size, sizeof(vector_size));
	invalidTxs.resize(vector_size);
        for (uint i = 0; i < vector_size; i++) {
            invalidTxs[i].Unserialize(s);
        }

	s.read((char*)&peerID, sizeof(peerID));
	s.read((char*)&sigSize, sizeof(sigSize));
	vchSig.resize(sigSize);
	s.read((char*)vchSig.data(), sigSize);
    }
    void getHash(uint256& result) const;
    void clear();
    bool empty() const;
};

#endif /* PBFT_MSG_H */

