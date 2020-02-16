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
#include <unordered_map>
#include "coins.h"
#include "validation.h"
#include "script/interpreter.h"
#include "uint256.h"
#include "primitives/transaction.h"

extern bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, std::vector<CScriptCheck> *pvChecks = nullptr);

enum CachedVerifyRes {COMMITTED, ABORTED, NOT_FOUND};
enum CoinState {UNSPENT, PENDING, SPENT};

/* state: the state of a coin, UNSPENT, PENDING, or SPENT. */
struct StateAndQueue{
    CoinState state;
    std::vector<uint32_t> waitingTxns;// the seq of waiting txs
};

class COutPointHasher{
public:
    std::size_t operator()(const COutPoint outpoint) const {
	return  outpoint.hash.GetCheapHash()^std::hash<uint32_t>{}(outpoint.n);
    }
};


class CTxProcessor{
public:
    std::unique_ptr<CCoinsViewDB> pcoinsSet;
    std::unique_ptr<CCoinsViewCache> pcoinsSetCache;
    std::unordered_map<uint64_t, bool> verifyResultsMap;
    /* TODO: add coin state map so that a coin can be marked as "pending" if 
     * verify succeed, "spent" if committed, and back to "unspent" if aborted. */
    //std::unordered_map<COutPoint, StateAndQueue> coinStateMap;
    std::unordered_map<COutPoint, StateAndQueue, COutPointHasher> coinStateMap;

    CTxProcessor();

    bool verifyTx(const CTransaction& tx, int seq);
    
    void executeTx(const CTransaction& tx, int seq);

    void abortTx(const CTransaction& tx, int seq);
    
    CachedVerifyRes fetchCachedVerifyRes(const uint256& txHash);
    void revertPreviousTxInStates(const CTransaction& tx, int i);
};

#endif /* TXPROCESSOR_H */

