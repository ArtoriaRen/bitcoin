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

#ifndef MSG_H
#define MSG_H
#include "uint256.h"
#include "primitives/transaction.h"

//global view number

enum PbftShardingPhase {PRE_PREPARE, PREPARE, COMMIT, DECISION_EXCHANGE, DECISION_DISSEMINATE, REPLY};

class PrePrepareMsg;

class Message {
private:
    bool voteCommit;
public:
    PbftShardingPhase phase;
    uint32_t view;
    uint32_t seq;
    uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    const static uint32_t messageSizeBytes = 128; // the real size is 4*4 + 32 +72 = 120 bytes.
    
    Message();

    Message(uint32_t senderId);
    
    Message(PbftShardingPhase p, uint32_t senderId, bool voteCommitIn);
    
    Message(PrePrepareMsg& pre_prepare, uint32_t senderId);
    
    void serialize(std::ostringstream& s, CTransactionRef clientReq = nullptr) const;
    
    void deserialize(std::istringstream& s, CTransactionRef clientReq = nullptr); 

    void getHash(uint256& result);

    bool getVoteCommit() const;

    void setVoteCommit(bool voteCommitIn);
};

class PrePrepareMsg : public Message{
    // CBlock block;
    /* we can use P2P network to disseminate the block before the primary send Pre_prepare msg 
     * so that the block does not have to be in the Pre-prepare message.*/
    
public:
    CTransactionRef clientReq;

    PrePrepareMsg():Message(){ }
    
    //add explicit?
    PrePrepareMsg(const Message& msg);

    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 

    const CTransaction& getTx();

};

class PrepareMsg: public Message{
public:
    PrepareMsg():Message(){
	phase = PbftShardingPhase::PREPARE;
    }
};

class CommitMsg: public Message{
public:
    CommitMsg():Message(){
	phase = PbftShardingPhase::COMMIT;
    }
};

/*Local pre-prepare message*/
class Reply {
public:
    PbftShardingPhase phase;
    uint32_t seq;
    uint32_t senderId;
    bool reply; // execution result:  1-commit or 0-abort
    uint256 digest; // use the block header hash as digest.
    /* TODO: change the YCSB workload (probably hash each key and value to constant size)
     * so that the reply has a fixed size.
     * Assume the reply is 1 byte for now.
     */
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    // the real size of a reply msg is 4*3 + 1 + 32 + 72 = 117 bytes.

    Reply();
    Reply(uint32_t seqNum, const uint32_t sender, bool rpl, const uint256& dgt);

    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 

    void getHash(uint256& result);
};

#endif /* MSG_H */

