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
#include <future>
#include "pbft/pbft.h"

extern std::atomic<uint32_t> totalTxSent;
extern bool sendingDone;
extern uint32_t num_committees;
extern int lastAssignedAffinity;
//extern uint32_t txStartBlock;
//extern uint32_t txEndBlock;
extern bool buildWaitGraph;


class DependencyRecord {
public:
    TxIndexOnChain tx;
    TxIndexOnChain prereq_tx;

    DependencyRecord();
    DependencyRecord(const uint32_t block_height, const uint32_t offset_in_block, const TxIndexOnChain& latest_prereq_tx_in);

    template<typename Stream>
    void Serialize(Stream& s) const{
	tx.Serialize(s);
	prereq_tx.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
	tx.Unserialize(s);
	prereq_tx.Unserialize(s);
    }
};

class TxPlacer{
public:
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum;
    /* tx couter for load balancing. */
    std::vector<int32_t> vTxCnt;

    /* return the number of shards that input UTXOs and output UTXOs span */
    TxPlacer();

    /* return the shard hosting the UTXO whose producing tx is txid */
    int32_t randomPlaceUTXO(const uint256& txid);

    /* return a vector of shard ids. 
     * The first element is the output shard id, and other elements are input shard ids.
     * The output shard id might equal one input shard id. */
    std::vector<int32_t> dependencyPlace(const CTransaction& tx, CCoinsViewCache& cache, std::vector<std::vector<uint32_t> >& vShardUtxoIdxToLock, const uint32_t block_height);
    std::vector<int32_t> smartPlace(const CTransaction& tx, CCoinsViewCache& cache, std::deque<std::vector<uint32_t> >& vShardUtxoIdxToLock, const uint32_t block_height);

    /* return the shard hosting the UTXO whose producing tx is txid. 
     * Note: this should be called only by servers, so the func does not 
     * search mapTxShard. */
    int32_t smartPlaceUTXO(const COutPoint& txin, const CCoinsViewCache& cache);

    void printPlaceResult();


    //uint32_t sendTxInBlock(uint32_t block_height, struct timeval& expected_last_send_time, int txSendPeriod);
    uint32_t sendTxInBlock(uint32_t block_height, int txSendPeriod);
    uint32_t sendAllTailTx(int txSendPeriod);

    /* return true if the tx is sent, false if the tx is queued. */
    bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height);
};

typedef struct {
    uint32_t idx;
    std::unordered_set<uint256, BlockHasher> prereqTxSet;
} WaitInfo;
	


void buildDependencyGraph(uint32_t block_height);

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

inline std::string getShardInfoFilename(int block_height) {
    return "/home/l27ren/shard_info_files/" + std::to_string(num_committees) + "committees/"+ std::to_string(block_height) + "_shardinfo.out";
}
void sendTxOfThread(const int startBlock, const int endBlock, const uint32_t thread_idx, const uint32_t num_threads, const int noop_count);
uint32_t sendAllTailTx(int noop_count);

extern TxPlacer g_txplacer;

inline std::string getDependencyFilename() {
    return "dependency_file/dependency.out";
}

#endif /* TX_PLACER_H */

