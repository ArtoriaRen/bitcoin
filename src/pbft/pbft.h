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
#include <set>

extern int32_t pbftID;
extern struct timeval thruInterval;

struct LockReply{
    std::map<int32_t, std::vector<CInputShardReply>> lockReply;
    std::atomic<char> decision;
};

enum TxType {SINGLE_SHARD, CROSS_SHARD};

struct TxStat{
    TxType type;
    struct timeval startTime;
};

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

class TxBlockInfo{
public:
    CTransactionRef tx;
    uint32_t blockHeight;
    uint32_t n;  // n-th tx in the block body
    int32_t outputShard; // Used for resolving to which shard a unlock_to_cmt req should be sent
    TxBlockInfo();
    TxBlockInfo(CTransactionRef txIn, uint32_t blockHeightIn, uint32_t nIn, int32_t outputShardIn = -1);
    friend bool operator<(const TxBlockInfo& a, const TxBlockInfo& b);
    friend bool operator>(const TxBlockInfo& a, const TxBlockInfo& b);
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


/*Thread-safe min heap*/
class ThreadSafeTxIndexSet{
public:

    /* this method does not requir the lock b/c it is only called by the init 
     * thread before tx-sending threads are created.
     */
    void lock_free_insert(const TxIndexOnChain& txIdx);

    /* remove an element. Called by msghand thread. */
    void erase(const TxIndexOnChain& txIdx);

    /* check if an element exist in the underlining set. 
     * Called by tx-sending thread. 
     */
    bool haveTx(const TxIndexOnChain& txIdx);

    size_t size();
    bool empty();

private:
    std::set<TxIndexOnChain> set_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class ThreadSafeVector {
public:
    ThreadSafeVector(uint32_t size, double initial_val);

    void add(uint32_t index, double value);
    /* return the index of the min element. */
    uint32_t minEleIndex();
    void print();

private:
    std::vector<double> vector_;
    std::mutex mutex_;
};

class ThreadSafePriorityQueue {
public:
    void getTxUpTo(const TxIndexOnChain& txIdx, std::deque<TxIndexOnChain>& res);
    void push(const TxIndexOnChain& txIdx);

private:
    std::priority_queue<TxIndexOnChain, std::deque<TxIndexOnChain>, std::greater<TxIndexOnChain>> pq_;
    std::mutex mutex_;
};


class CPbft{
public:
    static const uint32_t nFaulty = 1;
    static const size_t groupSize = 4;

    static const float LOAD_TX;
    static const float LOAD_LOCK;
    static const float LOAD_COMMIT;

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

    std::map<TxIndexOnChain, std::vector<TxIndexOnChain>> mapDependentTx; // <tx, all_dependent_tx>
    std::map<TxIndexOnChain, std::atomic<uint32_t>> mapRemainingPrereq; // <tx, num_remaining_uncommitted_prereq_tx_cnt>
    /* one priority for every sending thread. pq are heap-allocated by the thread 
     * answer rpc call sendtxinthreads. */
    std::vector<std::shared_ptr<ThreadSafePriorityQueue>> arrClearedTxQ; 
    
    std::ofstream latencySingleShardFile;
    std::ofstream latencyCrossShardFile;
    std::ofstream thruputFile;
    /* <txid, tx_start_time>
     * This map includes both single-shard and cross-shard tx.
     */
    ThreadSafeMap<uint256, TxStat, BlockHasher> mapTxStartTime;
    uint32_t nLastCompletedTx;
    std::atomic<uint32_t> nCompletedTx;
    std::atomic<uint32_t> nTotalFailedTx;
    struct timeval testStartTime;
    struct timeval nextLogTime;
    uint32_t nSucceed; /* number of single-shard committed tx */
    uint32_t nFail; /* number of single-shard aborted tx */
    uint32_t nCommitted; /* number of cross-shard committed tx */
    uint32_t nAborted; /* number of cross-shard aborte tx */
    ThreadSafeVector vLoad; // the load of all shards. 

    CPbft();
    ~CPbft();
    bool checkReplySig(const CReply* pReply) const;
    void logThruput(struct timeval& endTime);
    void loadDependencyGraph();

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

