/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft.h
 * Author: l27ren
 *
 * Created on June 16, 2020, 11:15 AM
 */

#ifndef PBFT_H
#define PBFT_H

#include <string>
#include <sys/time.h>
#include "net.h"
#include "validation.h"
#include "pbft_msg.h"
#include "pubkey.h"
#include "key.h"
#include <queue>  

extern int32_t pbftID;
extern uint32_t thruInterval;

struct LockReply{
    std::map<int32_t, std::vector<CInputShardReply>> lockReply;
    char decision;
};

enum TxType {SINGLE_SHARD, CROSS_SHARD};

struct TxStat{
    TxType type;
    struct timeval startTime;
};

class TxBlockInfo{
public:
    CTransactionRef tx;
    uint32_t blockHeight;
    uint32_t n;  // n-th tx in the block body
    bool aborted;
    uint32_t nAbortReplyShards;
    uint32_t nInputShards;
    TxBlockInfo();
    TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, uint32_t nInputShardsIn);
};

class ThreadSafeQueue {
public:
    ThreadSafeQueue();
    ~ThreadSafeQueue();

    TxBlockInfo& front();
    void pop_front();

    void push_back(const TxBlockInfo& item);
    void push_back(TxBlockInfo&& item);

    int size();
    bool empty();

private:
    std::deque<TxBlockInfo> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class CReplyBlockStat{
public:
    CReplyBlock replyBlk; // received from leader
    uint32_t nConfirm; //received from followers. Analyze the block when nConfirm == 2f

    CReplyBlockStat();
};

class CPbft{
public:
    static const uint32_t nFaulty = 1;
    static const size_t groupSize = 4;
    CPubKey myPubKey;
    std::vector<CNode*> leaders; // pbft leader
//    CPubKey myPubKey;
    std::unordered_map<int32_t, CPubKey> pubKeyMap;

    std::unordered_map<uint256, std::unordered_set<std::string>, BlockHasher> replyMap; // key is txid, value is a set of senders' addressName

    std::unordered_map<uint256, LockReply, BlockHasher> inputShardReplyMap; 

    std::unordered_map<uint256, uint256, BlockHasher> txUnlockReqMap; //  key is the digest of a unlock to commit or unlock to abort req, value is txid

    /* <txid, shard_ptr(tx)>
     * Every time we send a cross-shard tx to its input shards, we add an element to this map. 
     * This map is used when the input shards reply and we need to assemble a commit req. This map enables us to figure out the tx given a txid. */
    std::unordered_map<uint256, TxBlockInfo, BlockHasher> txInFly;
    ThreadSafeQueue txResendQueue;

    std::unordered_map<uint256, TxStat, BlockHasher> mapTxStartTime;
    std::unordered_map<uint256, CReplyBlockStat, BlockHasher> mapReplyBlockStat; // <block_merkle_root, <block, num_of_CReply>>

    uint32_t lastCompletedTx;
    uint32_t nCompletedTx; // tx that has been committed
    uint32_t nCommitNoResendTx; // tx that is commit for the first time they were sent. 
    uint32_t nAbortedTx; // tx that is aborted for the first time they were sent. 
    struct timeval thruStartTime;

    CPbft();
    bool checkReplySig(const CReply* pReply) const;
    bool checkReplyBlockSig(CReplyBlock* pReplyBlock) const;
    void logThruput(struct timeval& endTime);

private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

extern std::unique_ptr<CPbft> g_pbft;

#endif /* PBFT_H */

