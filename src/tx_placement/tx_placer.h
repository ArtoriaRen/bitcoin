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

extern uint32_t randomPlaceBlock;
extern uint32_t blockEnd;
extern uint32_t num_committees;
extern int lastAssignedAffinity;

/* place txs in a block */
class TxPlacer{
public:
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum = 0;

    /* return the number of shards that input UTXOs and output UTXOs span */
    TxPlacer(const CBlock& block);

    /* return a vector of shard ids. 
     * The first element is the output shard id, and other elements are input shard ids. */
    std::vector<int32_t> randomPlaceTxid(CTransactionRef tx);
    int32_t smartPlace(const CTransaction& tx, CCoinsViewCache& cache);
    void printPlaceResult();
    // TODO: smartPlaceSorted
};

/* place tx in the blockStart specified in conf file. This only work for 
 * existing blocks already on the chain.
 */
void randomPlaceTxInBlock();
void assignShardAffinity();
void incrementalAssignShardAffinity();
void printShardAffinity();
//void smartPlaceTxInBlocks();

/* place tx in the newly generated or received block. This only work for 
 * future blocks because we cannot assign shard affinity to input UTXOs of
 * historical tx since they had been spent and not exist in chainstate.
 */
void smartPlaceTxInBlock(const std::shared_ptr<const CBlock> pblock);
int32_t getShardAffinityForTx(CCoinsViewCache& cache, const CTransaction& tx);

#endif /* TX_PLACER_H */

