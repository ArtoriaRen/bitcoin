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
    int32_t outputShard; // Used for resolving to which shard a unlock_to_cmt req should be sent
    TxBlockInfo();
    TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, int32_t outputShardIn = -1);
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

template <typename K, typename V, typename Hasher>
class ThreadSafeMap {
public:
    ThreadSafeMap() { };
    ~ThreadSafeMap() { };

    V& operator[](const K& key) {
	std::unique_lock<std::mutex> mlock(mutex_);
	return map_[key];
    }
    
    bool exist(const K& key) {
	std::unique_lock<std::mutex> mlock(mutex_);
	if (map_.find(key) != map_.end())
	    return true;
	else
	    return false;
    }

    void insert(const std::pair<K, V>&& kvPair) {
	std::unique_lock<std::mutex> mlock(mutex_);
	map_.insert(kvPair);
    }

    void erase(const K& key) {
	std::unique_lock<std::mutex> mlock(mutex_);
	map_.erase(key);
    }

    int size() {
	std::unique_lock<std::mutex> mlock(mutex_);
	return map_.size();
    }
    
private:
    std::unordered_map<K, V, Hasher> map_;
    std::mutex mutex_;
    std::condition_variable cond_;
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

    //std::vector<CInputShardReply> vInputShardReplies;
    std::unordered_map<uint256, LockReply, BlockHasher> inputShardReplyMap; 

    std::unordered_map<uint256, uint256, BlockHasher> txUnlockReqMap; //  key is the digest of a unlock to commit or unlock to abort req, value is txid

    /* <txid, shard_ptr(tx)>
     * Every time we send a cross-shard tx to its input shards, we add an element to this map. 
     * This map is used when the input shards reply and we need to assemble a commit req.
     * This map enables us to figure out the tx given a txid. 
     */
    ThreadSafeMap<uint256, TxBlockInfo, BlockHasher> txInFly;
    
    ThreadSafeQueue txResendQueue;

    ThreadSafeMap<uint256, TxStat, BlockHasher> mapTxStartTime;
    uint32_t nLastCompletedTx;
    uint32_t nCompletedTx;
    uint32_t nTotalFailedTx;
    uint32_t nTotalSentTx;
    struct timeval testStartTime;
    struct timeval nextLogTime;

    CPbft();
    bool checkReplySig(const CReply* pReply) const;
    void logThruput(struct timeval& endTime);

private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

extern std::unique_ptr<CPbft> g_pbft;

#endif /* PBFT_H */

