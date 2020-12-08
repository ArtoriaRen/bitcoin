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

class TxPlacer{
public:
    /* return the shard hosting the UTXO which is an output of txid */
    int32_t randomPlaceUTXO(const uint256& txid);
};

#endif /* TX_PLACER_H */

