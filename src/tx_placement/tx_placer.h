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

class TxPlacer{
public:
    uint32_t num_committees;

    TxPlacer(uint32_t numCommitteesIn = 2);
    /* return the number of shards that input UTXOs and output UTXOs span */
    int randomPlaceTxidIndex(CTransactionRef tx);
    int randomPlaceTxid(CTransactionRef tx);
    int smartPlace(CTransactionRef tx);
};


void placeTxInBlocks(uint blockStart, uint blockEnd);

#endif /* TX_PLACER_H */

