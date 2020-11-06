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
#include <iostream>
#include <fstream>

extern int32_t pbftID;
extern struct timeval thruInterval;

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
    TxBlockInfo();
    TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn);
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

    /* <single-shard txid or cross-shard commit or abort req disgest,
     *  set_of_reply-sender_addressName>
     * Used to decide if a tx has recive enough reply that the client can deduce
     * the tx has been committed or aborted by servers.
     * This map includes both single-shard and cross-shard tx.
     */
    ThreadSafeMap<uint256, std::unordered_set<std::string>, BlockHasher> replyMap; 

    /* <txid, <shardId, vector_of_reply_from_the_shard>
     * When we receive a lock reply, we add it to this map so that we know when
     * we have accumulate enough lock reply to decide a commit or abort req should
     * be sent. The lock replies of a tx also serves as proofs of future commit 
     * or abort req.
     * This map only includes cross-shard tx.
     */
    ThreadSafeMap<uint256, LockReply, BlockHasher> inputShardReplyMap; 

    /* <digest_of_commit_or_abort_req, txid> 
     * When a reply of an commit or abort req reply is received, this map helps us to 
     * figure out the corresponding txid so that we can search the start time of
     * this tx in the mapTxStartTime.
     * This map only includes cross-shard tx.
     */
    ThreadSafeMap<uint256, uint256, BlockHasher> txUnlockReqMap; 

    /* <txid, shard_ptr(tx)>
     * Every time we send a cross-shard tx to its input shards, we add an element to this map. 
     * This map enables us to figure out the tx given a txid. 
     * We need the tx when we assemble a commit or abort req, or want to resend 
     * the tx.
     * This map only includes cross-shard tx.
     */
    ThreadSafeMap<uint256, TxBlockInfo, BlockHasher> txInFly;
    
    ThreadSafeQueue txResendQueue;

    std::ofstream latencyFile;
    std::ofstream thruputFile;
    /* <txid, tx_start_time>
     * This map includes both single-shard and cross-shard tx.
     */
    ThreadSafeMap<uint256, TxStat, BlockHasher> mapTxStartTime;
    uint32_t nLastCompletedTx;
    std::atomic<uint32_t> nCompletedTx;
    std::atomic<uint32_t> nTotalFailedTx;
    uint32_t nTotalSentTx;
    struct timeval testStartTime;
    struct timeval nextLogTime;

    CPbft();
    ~CPbft();
    bool checkReplySig(const CReply* pReply) const;
    void logThruput(struct timeval& endTime);

private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

inline struct timeval operator+(const struct timeval& t0, const struct timeval& t1)
{
    struct timeval t = {t0.tv_sec + t1.tv_sec, t0.tv_usec + t1.tv_usec};
    if (t.tv_usec >= 1000000) { // carry needed
	    t.tv_sec++;
	    t.tv_usec -= 1000000;
    }
    return t;
}

inline struct timeval operator-(const struct timeval& t0, const struct timeval& t1)
{
    struct timeval t = {t0.tv_sec - t1.tv_sec, t0.tv_usec - t1.tv_usec};
    if (t.tv_usec < 0) { // borrow needed
	    t.tv_sec--;
	    t.tv_usec += 1000000;
    }
    return t;
}

inline bool operator>=(const struct timeval& t0, const struct timeval& t1) {
    return (t0.tv_sec > t1.tv_sec) || (t0.tv_sec == t1.tv_sec && t0.tv_usec >= t1.tv_usec);
}

extern std::unique_ptr<CPbft> g_pbft;

#endif /* PBFT_H */

