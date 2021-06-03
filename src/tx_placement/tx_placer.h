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
#include <future>
#include "pbft/pbft.h"

extern std::atomic<uint32_t> totalTxSent;
extern std::atomic<uint32_t> globalReqSentCnt;
extern bool sendingDone;
extern uint32_t num_committees;
extern float optchain_LB_weight;
/* once the max load score difference/ average load score grows greater 
 * than this threshold, assign tx to its lowest-load-score input shard. 
 */
extern float mostUTXO_LB_thld;
extern int lastAssignedAffinity;
//extern uint32_t txStartBlock;
//extern uint32_t txEndBlock;


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

class uint256Hasher {
public:

    size_t operator()(const uint256& id) const {
        return id.GetCheapHash();
    }
};

class CPlacementStatus {
public:
    std::vector<float> fitnessScore;
    uint32_t numOutTx;
    uint32_t numUnspentCoin; 
    int32_t placementRes;
    CTransactionRef txRef;
    CPlacementStatus();
    CPlacementStatus(uint32_t num_upspent_coin);
};

class TxPlacer{
public:
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum;

    /* OptChain data stuctures*/
    float alpha;
    /* key is txid, value is the p'(v) in the OptChain paper and the remaing number
     * of unspent coins of the tx. If the count becomes 0, the tx is removed from 
     * this map because future tx placement does not need their fitness score 
     * anymore. 
     */
    std::unordered_map<uint256, CPlacementStatus, uint256Hasher> mapNotFullySpentTx;

    /* return the number of shards that input UTXOs and output UTXOs span */
    TxPlacer();

    /* return a vector of shard ids. 
     * The first element is the output shard id, and other elements are input shard ids. */
    std::vector<int32_t> randomPlace(const CTransaction& tx);

    /* return the shard hosting the UTXO whose producing tx is txid */
    int32_t randomPlaceUTXO(const uint256& txid);

    /* place a tx using the OptChain algorithm */
    /* The first element of the returned vector is the output shard id, and 
     * other elements are input shard ids. 
     * The vShardUtxoIdxToLock is filled with input utxo index to lock for each input
     * shard.
     */
    std::vector<int32_t> hashingPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> optchainPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> mostInputUTXOPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> mostInputValuePlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> firstUtxoPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> HPtoInputShard(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> HPOnlyCrossShardTx(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> optchainPlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> mostInputUTXOPlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> mostInputValuePlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    std::vector<int32_t> firstUtxoPlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock);

    uint countInputTx(const CTransactionRef pTx);

    void printPlaceResult();
    void printTxSendRes();
};

void sendTxOfThread(const int startBlock, const int endBlock, const uint32_t thread_idx, const uint32_t num_threads, const int noop_count, const uint placementMethod);

uint32_t sendAllTailTx(int noop_count);

/* return true if the tx is sent, false if the tx is queued. */
bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height, std::vector<std::deque<std::shared_ptr<CClientReq>>>& batchBuffers, uint32_t& reqSentCnt, TxPlacer& txPlacer, const uint placementMethod);

inline std::string getDependencyFilename() {
    return "dependency_file/dependency.out";
}

#endif /* TX_PLACER_H */

