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

extern uint32_t blockStart;
extern uint32_t blockEnd;
extern uint32_t num_committees;

//extern int lastAssignedAffinity;

class TxPlacer{
public:
    /* return the number of shards that input UTXOs and output UTXOs span */
//    int randomPlaceTxidIndex(CTransactionRef tx);
    int randomPlaceTxid(CTransactionRef tx);
    int smartPlace(CTransactionRef tx);
    // TODO: smartPlaceSorted
};

/* place tx in the blockStart specified in conf file. This only work for 
 * existing blocks already on the chain.
 */
void randomPlaceTxInBlock();
void assignShardAffinity();
void printChainAffinity();
//void smartPlaceTxInBlocks();

/* place tx in the newly generated or received block. This only work for 
 * future blocks because we cannot assign shard affinity to input UTXOs of
 * historical tx since they had been spent and not exist in chainstate.
 */
void smartPlaceTxInBlock(const std::shared_ptr<const CBlock> pblock);
int32_t getShardAffinityForTx(const CTransaction& tx);

#endif /* TX_PLACER_H */

