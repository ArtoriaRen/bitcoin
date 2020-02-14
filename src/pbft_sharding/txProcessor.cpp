/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft_sharding/txProcessor.h"

CTxProcessor::CTxProcessor(){
    pcoinsSet.reset(new CCoinsViewDB(1 << 23, true)); 
    pcoinsSetCache.reset(new CCoinsViewCache(pcoinsSet.get())); 
}

bool CTxProcessor::verifyTx(const CTransaction& tx){
	bool fCacheResults = false;
	PrecomputedTransactionData txdata(tx);
        CValidationState state;
	/* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
	if (!CheckInputs(tx, state, *pcoinsSetCache, true, SCRIPT_VERIFY_NONE, fCacheResults, fCacheResults, txdata)) {
	    return error("verifyTx(): CheckInputs on %s failed with %s",
                    tx.GetHash().ToString(), FormatStateMessage(state));
	} 
	return true;
}

bool CTxProcessor::executeTx(const CTransaction& tx){

}