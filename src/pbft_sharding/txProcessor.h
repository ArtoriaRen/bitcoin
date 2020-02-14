/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   txProcessor.h
 * Author: l27ren
 *
 * Created on February 14, 2020, 10:21 AM
 */

#ifndef TXPROCESSOR_H
#define TXPROCESSOR_H

#include <consensus/validation.h>
#include <txdb.h>
#include "coins.h"
#include "validation.h"
#include "script/interpreter.h"

extern bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, std::vector<CScriptCheck> *pvChecks = nullptr);

class CTxProcessor{
public:
    std::unique_ptr<CCoinsViewDB> pcoinsSet;
    std::unique_ptr<CCoinsViewCache> pcoinsSetCache;
    //TODO: add coin state map so that a coin can be marked as spent is verify succeed.

    CTxProcessor();

    
    bool verifyTx(const CTransaction& tx);
    
    bool executeTx(const CTransaction& tx);
};

#endif /* TXPROCESSOR_H */

