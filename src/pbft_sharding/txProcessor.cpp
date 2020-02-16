/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft_sharding/txProcessor.h"

void CTxProcessor::revertPreviousTxInStates(const CTransaction& tx, int endCoinIndex) {
    for (int i = 0; i < endCoinIndex; i++) {
	// we deem the coin exists, otherwise it will not reach to input coin i;
	coinStateMap[tx.vin[i].prevout].state = CoinState::UNSPENT;
    }
}

CTxProcessor::CTxProcessor(){
    pcoinsSet.reset(new CCoinsViewDB(1 << 23, true)); 
    pcoinsSetCache.reset(new CCoinsViewCache(pcoinsSet.get())); 
}

bool CTxProcessor::verifyTx(const CTransaction& tx, int seq){
    bool fCacheResults = false;
    PrecomputedTransactionData txdata(tx);
    CValidationState state;
    
    /* check if all coins are in unspent state */
    for (uint i = 0; i < tx.vin.size(); i++) {
	CTxIn inputCoin = tx.vin[i];
	if (coinStateMap.find(inputCoin.prevout) == coinStateMap.end()) {
	    std::cout << "tx input not found: " << inputCoin.prevout.ToString() 
		    << std::endl;
	    revertPreviousTxInStates(tx, i);
	    return false;
	} 
	if (coinStateMap[inputCoin.prevout].state == CoinState::SPENT){
	    std::cout << "tx input already spent: " << inputCoin.prevout.ToString() 
		    << std::endl;
	    revertPreviousTxInStates(tx, i);
	    return false;
	}
	if (coinStateMap[inputCoin.prevout].state == CoinState::PENDING){
	    std::cout << "tx input is in pending state: " << inputCoin.prevout.ToString() 
		    << std::endl;
	    // add tx to the waiting queue of this coin.
	    coinStateMap[inputCoin.prevout].waitingTxns.push_back(seq);
	    return false;
	}
	if (coinStateMap[inputCoin.prevout].state == CoinState::UNSPENT){
	    // mark the coin as pending so that other tx cannot spend the coin.
	    coinStateMap[inputCoin.prevout].state == CoinState::PENDING;
	    return true;
	}
    }
    
    /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
    if (!CheckInputs(tx, state, *pcoinsSetCache, true, SCRIPT_VERIFY_NONE, fCacheResults, fCacheResults, txdata)) {
	return error("verifyTx(): CheckInputs on %s failed with %s",
		tx.GetHash().ToString(), FormatStateMessage(state));
    } 
    return true;
}

//TODO: add another thread dedicating execute transaction sequentially.
void CTxProcessor::executeTx(const CTransaction& tx, int seq){
    // we use sequence number as block height
    UpdateCoins(tx, *pcoinsSetCache, seq);
}

void CTxProcessor::abortTx(const CTransaction& tx, int seq) {
    // mark all input coins back to unspent state

    /* if there is any tx in the waiting queue, try unblock it. 
     * Some tx in the queue might still be blocked on other coins.
     */ 

    /* Our consensus protocol does not change decision once decides commit or abort,
     * so there is no need to undo the tx since we have never executed it.
     */
}
CachedVerifyRes CTxProcessor::fetchCachedVerifyRes(const uint256& txHash) {
    uint64_t shortHash = txHash.GetCheapHash();
    if (verifyResultsMap.find(shortHash) == verifyResultsMap.end())
	return CachedVerifyRes::NOT_FOUND;
    if (verifyResultsMap[shortHash])
	return CachedVerifyRes::COMMITTED;
    return CachedVerifyRes::ABORTED;
}