/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   tx_placer.h
 * Author: l27ren
 *
 * Created on June 5, 2020, 1:58 PM
 */

#ifndef TX_PLACER_H
#define TX_PLACER_H

#include <primitives/transaction.h>
#include <primitives/block.h>
#include <coins.h>
#include "validation.h"
#include <queue>

extern uint32_t num_committees;
extern int lastAssignedAffinity;
//extern uint32_t txStartBlock;
//extern uint32_t txEndBlock;
extern bool buildWaitGraph;

class TxPlacer{
public:
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum;

    /* return the number of shards that input UTXOs and output UTXOs span */
    TxPlacer();

    /* return a vector of shard ids. 
     * The first element is the output shard id, and other elements are input shard ids. */
    std::vector<int32_t> randomPlace(const CTransaction& tx);

    /* return the shard hosting the UTXO whose producing tx is txid */
    int32_t randomPlaceUTXO(const uint256& txid);

    /* return a vector of shard ids. 
     * The first element is the output shard id, and other elements are input shard ids.
     * The output shard id might equal one input shard id. */
    std::vector<int32_t> smartPlace(const CTransaction& tx, CCoinsViewCache& cache, std::vector<std::vector<uint32_t> >& vShardUtxoIdxToLock);

    /* return the shard hosting the UTXO whose producing tx is txid. 
     * Note: this should be called only by servers, so the func does not 
     * search mapTxShard. */
    int32_t smartPlaceUTXO(const COutPoint& txin, const CCoinsViewCache& cache);

    void printPlaceResult();
    // TODO: smartPlaceSorted
};

class ThreadSafeIntPQ{
public:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> pq_;

    ThreadSafeIntPQ();
    void push(const uint32_t val);
    std::vector<uint32_t> pop_upto(const uint32_t upto);
};

//uint32_t sendTxInBlock(uint32_t block_height, struct timeval& expected_last_send_time, int txSendPeriod);
uint32_t sendTxInBlock(uint32_t block_height, int txSendPeriod);

/* return true if the tx is sent, false if the tx is queued. */
bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height);

void buildDependencyGraph(uint32_t block_height, std::map<uint32_t, std::unordered_set<uint256, BlockHasher>>& waitForGraph);

/* place tx in the blockStart specified in conf file. This only work for 
 * existing blocks already on the chain.
 */
void randomPlaceTxInBlock();

/* This function read all UTXOs from disk and assign a chainAffinity to them,
 * and then save them back to the disk.
 * We must disable ::Unserialize(s, shardAffinity); when unserialize a coin.
 * See the Unserialize function in coins.h 
 */
void assignShardAffinity();
void incrementalAssignShardAffinity();
void printShardAffinity();
//void smartPlaceTxInBlocks();
void extractRawTxInBlock();

/* place tx in the newly generated or received block. This only work for 
 * future blocks because we cannot assign shard affinity to input UTXOs of
 * historical tx since they had been spent and not exist in chainstate.
 */
void smartPlaceTxInBlock(const std::shared_ptr<const CBlock> pblock);

extern std::unique_ptr<ThreadSafeIntPQ> g_prereqClearTxPQ; // a priority queue for prerequiste-tx-clear dependent tx
extern std::unique_ptr<std::map<uint32_t, std::unordered_set<uint256, BlockHasher>>> g_waitForGraph;

#endif /* TX_PLACER_H */

