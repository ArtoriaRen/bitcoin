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

extern uint32_t randomPlaceBlock;
extern uint32_t blockEnd;
extern uint32_t num_committees;
extern int lastAssignedAffinity;

class TxPlacer{
public:
    std::map<uint, std::map<uint, uint>> shardCntMap; // < input_utxo_count, shard_count, tx_count>
    uint totalTxNum;

    /* A map for 2PC coordinator to figure out where to send lockReq if a tx spends
     * UTXOs generated during the test. 
     */
    std::unordered_map<uint256, int32_t, BlockHasher> mapTxShard; // <txid, shardID>

    /* return the number of shards that input UTXOs and output UTXOs span */
    TxPlacer();

    /* return a vector of shard ids. 
     * The first element is the output shard id, and other elements are input shard ids.
     * The output shard id might equal one input shard id. */
    std::vector<int32_t> randomPlace(const CTransaction& tx);

    /* return the shard hosting the UTXO whose producing tx is txid */
    int32_t randomPlaceUTXO(const uint256& txid);

    void printPlaceResult();
    // TODO: smartPlaceSorted
};

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

#endif /* TX_PLACER_H */

