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
    /* verify and execute prereq-clear tx in this block. */
    uint32_t Verify(const int seq, CCoinsViewCache& view, std::vector<char>& validTxs, std::vector<uint32_t>& invalidTxs) const;
    uint32_t Execute(const int seq, CCoinsViewCache& view) const;
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

class CPre_prepare : public CPbftMessage{
public:
    std::shared_ptr<CPbftBlock> pPbftBlock;

    CPre_prepare();
    CPre_prepare(std::shared_ptr<CPbftBlock> pPbftBlockIn):CPbftMessage(), pPbftBlock(pPbftBlockIn) { }
    CPre_prepare(const CPbftMessage& pbftMsg, std::shared_ptr<CPbftBlock> pPbftBlockIn):CPbftMessage(pbftMsg), pPbftBlock(pPbftBlockIn) { }
    
    CPre_prepare(const CPre_prepare& msg);
    CPre_prepare(const CPbftMessage& msg);

    template<typename Stream>
    void Serialize(Stream& s) const{
	CPbftMessage::Serialize(s);
	pPbftBlock->Serialize(s);
    }
    
    template<typename Stream>
    void Unserialize(Stream& s) {
	CPbftMessage::Unserialize(s);
	pPbftBlock->Unserialize(s);
    }
};

class CCollabMessage {
public:
    uint32_t height;
    std::vector<char> validTxs;
    std::vector<uint32_t> invalidTxs;
    int32_t peerID;
    uint32_t sigSize;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.

    CCollabMessage();
    CCollabMessage(uint32_t height, std::vector<char>&& validTxs, std::vector<uint32_t>&& invalidTxs);
    
    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write((char*)&height, sizeof(height));
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

bool VerifyTx(const CTransaction& tx, const int seq, CCoinsViewCache& view);
bool VerifyButNoExecuteTx(const CTransaction& tx, const int seq, CCoinsViewCache& view);
bool ExecuteTx(const CTransaction& tx, const int seq, CCoinsViewCache& view);

#endif /* PBFT_MSG_H */

