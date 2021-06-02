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
    /* key is shard id, value is the CInputShardReply received from the shard. */
    std::map<int32_t, std::vector<CInputShardReply>> lockReply;
    std::atomic<char> decision;
};

enum TxType {SINGLE_SHARD, CROSS_SHARD};

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

class TxStat {
public:
    CTransactionRef tx;
    /* Used for resolving to which shard a unlock_to_cmt req should be sent */
    int32_t outputShard; 
    struct timeval startTime;
    
    TxStat();
    TxStat(CTransactionRef txIn, int32_t outputShardIn = -1);
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

    void emplace(const K key, const V value) {
	std::unique_lock<std::mutex> mlock(mutex_);
	map_.emplace(key, value);
    }

    void erase(const K& key) {
	std::unique_lock<std::mutex> mlock(mutex_);
	map_.erase(key);
    }

    int size() {
	std::unique_lock<std::mutex> mlock(mutex_);
	return map_.size();
    }
    
    std::unordered_map<K, V, Hasher> map_;
private:
    std::mutex mutex_;
    std::condition_variable cond_;
};

class CShardLatency {
public:
    struct timeval probe_send_time;
    /* the sum of communication latency and tx verification latency. */
    std::atomic<uint> latency; // in us 
    CShardLatency();
};

class CPbft{
public:
    static const uint32_t nFaulty = 1;
    static const size_t groupSize = 4;

    CPubKey myPubKey;
    std::vector<CNode*> leaders; // pbft leader
//    CPubKey myPubKey;
    std::deque<CPubKey> pubKeys;

    /* <single-shard txid or cross-shard commit or abort req disgest,
     *  set_of_reply-sender_addressName>
     * Used to decide if a tx has recive enough reply that the client can deduce
     * the tx has been committed or aborted by servers.
     * This map includes both single-shard and cross-shard tx.
     */
    ThreadSafeMap<uint256, uint32_t, BlockHasher> replyMap; 

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

    std::deque<CBlock> blocks2Send;
    
    std::ofstream latencySingleShardFile;
    std::ofstream latencyCrossShardFile;
    std::ofstream thruputFile;
    std::ofstream shardLoadFile;
    std::ofstream recordedSentTx;
    /* <txid, tx_start_time>
     * This map includes both single-shard and cross-shard tx.
     */
    ThreadSafeMap<uint256, TxStat, BlockHasher> mapTxStat;
    uint32_t nLastCompletedTx;
    std::atomic<uint32_t> nCompletedTx;
    std::atomic<uint32_t> nTotalFailedTx;
    struct timeval testStartTime;
    struct timeval nextLogTime;
    struct timeval nextShardLoadPrintTime;
    std::atomic<uint32_t> nSucceed; /* number of single-shard committed tx */
    std::atomic<uint32_t> nFail; /* number of single-shard aborted tx */
    std::atomic<uint32_t> nCommitted; /* number of cross-shard committed tx */
    std::atomic<uint32_t> nAborted; /* number of cross-shard aborte tx */
    std::vector<CReqBatch> batchBuffers;
    std::vector<std::mutex> vBatchBufferMutex; /* guard access to batchBuffers by tx_sending threads and the msg_pushing thread. */
    std::vector<CShardLatency> expected_tx_latency;
    uint placementMethod;
    /* the number of tx assigned to each shard. */
    std::vector<uint> vecShardTxCount;
    /* the load score of each shard. */
    std::vector<uint> loadScores;
    uint nSingleShard; /* number of single shard tx based on the placement results. */
    uint nCrossShard; /* number of cross shard tx based on the placement results. */

    CPbft();
    ~CPbft();
    bool checkReplySig(const CReply* pReply) const;
    void logThruput(struct timeval& endTime);
    void add2Batch(const uint32_t shardId, const ClientReqType type, const CTransactionRef txRef, std::deque<std::shared_ptr<CClientReq>>& threadLocalBatchBuffer, const std::vector<uint32_t>* utxoIdxToLock = nullptr);
    void add2BatchOnlyBuffered(const uint32_t shardId, std::deque<std::shared_ptr<CClientReq>>& threadLocalBatchBuffer);
    /* called by the rpc thread to load all blocks about to send. */
    void loadBlocks(uint32_t startBlock, uint32_t endBlock);
    void probeShardLatency();
    void logShardLoads(struct timeval& endTime);
    void updateLoadScore(uint shard_id, ClientReqType reqType, uint nSigs);

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

void sendAllBatch();

extern std::unique_ptr<CPbft> g_pbft;

#endif /* PBFT_H */

