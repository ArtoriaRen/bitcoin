/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */


#include "pbft/pbft_msg.h"
#include "hash.h"
#include "pbft.h"
#include "validation.h"
#include "consensus/validation.h"
#include "consensus/params.h"
#include "consensus/tx_verify.h"
#include "coins.h"
#include "script/interpreter.h"
#include "undo.h"

void UpdateLockCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight);

std::unordered_map<uint256, CTxUndo, BlockHasher> mapTxUndo;

CPbftMessage::CPbftMessage(): view(0), seq(0), digest(), sigSize(0), vchSig(){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CPbftMessage::CPbftMessage(const CPbftMessage& msg): view(msg.view), seq(msg.seq), digest(msg.digest), sigSize(msg.sigSize), vchSig(msg.vchSig){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CPbftMessage::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&view, sizeof(view))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)digest.begin(), digest.size())
	    .Finalize((unsigned char*)&result);
}

CPre_prepare::CPre_prepare(const CPre_prepare& msg): CPbftMessage(msg), req(msg.req) { }

CPre_prepare::CPre_prepare(const CPbftMessage& msg): CPbftMessage(msg) { }

CReply::CReply(): reply(), digest(), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CReply::CReply(char replyIn, const uint256& digestIn): reply(replyIn), digest(digestIn), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CReply::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&reply, sizeof(reply))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}

void TxReq::Execute(const int seq) const {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(tx_mutable);
    
    CValidationState state;
    CCoinsViewCache view(pcoinsTip.get());
    bool fScriptChecks = true;
//	    CBlockUndo blockundo;
    unsigned int flags = SCRIPT_VERIFY_NONE; // only verify pay to public key hash
    CAmount txfee = 0;
    /* We use  INT_MAX as block height, so that we never fail coinbase 
     * maturity check. */
    if (!Consensus::CheckTxInputs(tx, state, view, INT_MAX, txfee)) {
	std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
	return;
    }

    // GetTransactionSigOpCost counts 3 types of sigops:
    // * legacy (always)
    // * p2sh (when P2SH enabled in flags and excludes coinbase)
    // * witness (when witness enabled in flags and excludes coinbase)
    int64_t nSigOpsCost = 0;
    nSigOpsCost += GetTransactionSigOpCost(tx, view, flags);
    if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST) { 
	std::cerr << __func__ << ": ConnectBlock(): too many sigops" << std::endl;
	return;
    }

    PrecomputedTransactionData txdata(tx);
    std::vector<CScriptCheck> vChecks;
    bool fCacheResults = false; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
    if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata, nullptr)) {  // do not use multithreads to check scripts
	std::cerr << __func__ << ": ConnectBlock(): CheckInputs on " 
		<< tx.GetHash().ToString() 
		<< " failed with " << FormatStateMessage(state)
		<< std::endl;
	return;
    }

//	    CTxUndo undoDummy;
//	    if (i > 0) {
//		blockundo.vtxundo.push_back(CTxUndo());
//	    }
    UpdateCoins(tx, view, seq);
    bool flushed = view.Flush(); // flush to pcoinsTip
    assert(flushed);
    /* -------------logic from Bitcoin code for tx processing--------- */

    std::cout << __func__ << ": excuted tx " << tx.GetHash().ToString()
	    << " at log slot " << seq << std::endl;

}

uint256 TxReq::GetDigest() const {
    return tx_mutable.GetHash();
}


void LockReq::Execute(const int seq) const {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(tx_mutable);
    
    CValidationState state;
    CCoinsViewCache view(pcoinsTip.get());
    bool fScriptChecks = true;
//	    CBlockUndo blockundo;
    unsigned int flags = SCRIPT_VERIFY_NONE; // only verify pay to public key hash

    /* Step 1: find all input UTXOs whose chainAffinity is our shard. Check if they are unspent.
     * We use INT_MAX as block height, so that we never fail coinbase maturity check. */
    if (!Consensus::CheckLockReqInputs(tx, state, view, INT_MAX, totalValueInOfShard)) {
	std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
	return;
    }

    /* Step 2: count sig ops. Do this in the output shard. */
    // GetTransactionSigOpCost counts 3 types of sigops:
    // * legacy (always)
    // * p2sh (when P2SH enabled in flags and excludes coinbase)
    // * witness (when witness enabled in flags and excludes coinbase)
//    int64_t nSigOpsCost = 0;
//    nSigOpsCost += GetTransactionSigOpCost(tx, view, flags);
//    if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST) { 
//	std::cerr << __func__ << ": ConnectBlock(): too many sigops" << std::endl;
//	return;
//    }

    /* Step 3: check sigScript for input UTXOs in our shard.*/
    PrecomputedTransactionData txdata(tx);
    std::vector<CScriptCheck> vChecks;
    bool fCacheResults = false; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
    if (!CheckLockInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata, nullptr)) {  // do not use multithreads to check scripts
	std::cerr << __func__ << ": ConnectBlock(): CheckInputs on " 
		<< tx.GetHash().ToString() 
		<< " failed with " << FormatStateMessage(state)
		<< std::endl;
	return;
    }

//	    CTxUndo undoDummy;
//	    if (i > 0) {
//		blockundo.vtxundo.push_back(CTxUndo());
//	    }
    /* Step 4: spent the input coins in our shard and store them in the global map 
     * for possibly future UnlockToAbort processing. */
    CTxUndo txUndo;
    UpdateLockCoins(tx, view, txUndo, seq);
    mapTxUndo.insert(std::make_pair(tx.GetHash(), txUndo));
    bool flushed = view.Flush(); // flush to pcoinsTip
    assert(flushed);
    /* -------------logic from Bitcoin code for tx processing--------- */
    std::cout << __func__ << ": locked UTXOs at log slot " << seq << std::endl;
}

uint256 LockReq::GetDigest() const {
    return tx_mutable.GetHash();
}

CInputShardReply::CInputShardReply(): CReply() {};

CInputShardReply::CInputShardReply(char replyIn, const uint256& digestIn, CAmount valueIn): 
    CReply(replyIn, digestIn), totalValueInOfShard(valueIn) {};

void CInputShardReply::getHash(uint256& result){
    uint256 tmp;
    CReply::getHash(tmp);
    CHash256().Write((const unsigned char*)tmp.begin(), tmp.size())
	    .Write((const unsigned char*)&totalValueInOfShard, sizeof(totalValueInOfShard))
	    .Finalize((unsigned char*)&result);
}