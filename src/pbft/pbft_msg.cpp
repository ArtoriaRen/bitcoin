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
#include "netmessagemaker.h"


void UpdateLockCoins(const CTransaction& tx, const std::vector<uint32_t>& vInputUtxoIdxToLock, CCoinsViewCache& inputs, TxUndoInfo& undoInfo, int nHeight);

void UpdateUnlockCommitCoins(const CTransaction& tx, CCoinsViewCache& inputs, int nHeight);

void UpdateUnlockAbortCoins(const CTransaction& tx, CCoinsViewCache& inputs, TxUndoInfo& undoInfo);



/* key is txid, value is input Coins in our shard. */
static std::unordered_map<uint256, TxUndoInfo, BlockHasher> mapTxUndo;

CPbftMessage::CPbftMessage(): view(0), seq(0), digest(), peerID(pbftID), sigSize(0), vchSig(){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CPbftMessage::CPbftMessage(const CPbftMessage& msg): view(msg.view), seq(msg.seq), digest(msg.digest), peerID(pbftID), sigSize(msg.sigSize), vchSig(msg.vchSig){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CPbftMessage::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&view, sizeof(view))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)digest.begin(), digest.size())
	    .Finalize((unsigned char*)&result);
}

CPre_prepare::CPre_prepare(const CPre_prepare& msg): CPbftMessage(msg), pbft_block(msg.pbft_block) { }

CPre_prepare::CPre_prepare(const CPbftMessage& msg): CPbftMessage(msg) { }

CReply::CReply(): reply(), digest(), peerID(pbftID), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CReply::CReply(char replyIn, const uint256& digestIn): reply(replyIn), digest(digestIn), peerID(pbftID), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CReply::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&reply, sizeof(reply))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}


CClientReq::CClientReq(const CTransaction& tx): tx_mutable(tx), hash(tx.GetHash()) { }

void CClientReq::UpdateHash() {
    hash = tx_mutable.GetHash();
}

const uint256& CClientReq::GetHash() const {
    return hash;
}

char TxReq::Execute(const int seq, CCoinsViewCache& view) const {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(tx_mutable);
    
    CValidationState state;
    if(!tx.IsCoinBase()) {
	bool fScriptChecks = true;
	unsigned int flags = SCRIPT_VERIFY_NONE; // only verify pay to public key hash
	CAmount txfee = 0;
	/* We use  INT_MAX as block height, so that we never fail coinbase 
	 * maturity check. */
	if (!Consensus::CheckTxInputs(tx, state, view, INT_MAX, txfee)) {
	    std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
	    return 'n';
	}

	// GetTransactionSigOpCost counts 3 types of sigops:
	// * legacy (always)
	// * p2sh (when P2SH enabled in flags and excludes coinbase)
	// * witness (when witness enabled in flags and excludes coinbase)
	int64_t nSigOpsCost = 0;
	nSigOpsCost += GetTransactionSigOpCost(tx, view, flags);
	if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST) { 
	    std::cerr << __func__ << ": ConnectBlock(): too many sigops" << std::endl;
	    return 'n';
	}

	PrecomputedTransactionData txdata(tx);
	std::vector<CScriptCheck> vChecks;
	bool fCacheResults = false; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
	if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata, nullptr)) {  // do not use multithreads to check scripts
	    std::cerr << __func__ << ": ConnectBlock(): CheckInputs on " 
		    << tx.GetHash().ToString() 
		    << " failed with " << FormatStateMessage(state)
		    << std::endl;
	    return 'n';
	}
    }

    UpdateCoins(tx, view, seq);
    /* -------------logic from Bitcoin code for tx processing--------- */

    std::cout << __func__ << ": excuted tx " << tx.GetHash().ToString()
	    << " at log slot " << seq << std::endl;
    return 'y';
}

uint256 TxReq::GetDigest() const {
    return tx_mutable.GetHash();
}


char LockReq::Execute(const int seq, CCoinsViewCache& view) const {
    /* we did not check if there is any input coins belonging our shard because
     * we believe the client is honest and will not send an LOCK req to a 
     * irrelavant shard. In OmniLedger, we already beleive in the client to be 
     * the 2PC coordinator so this assumption does not put more trust on the client
     */

    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(tx_mutable);
    
    CValidationState state;
    bool fScriptChecks = true;
//	    CBlockUndo blockundo;
    unsigned int flags = SCRIPT_VERIFY_NONE; // only verify pay to public key hash

    /* Step 1: find all input UTXOs whose chainAffinity is our shard. Check if they are unspent.
     * We use INT_MAX as block height, so that we never fail coinbase maturity check. */
    if (!Consensus::CheckLockReqInputs(tx, state, view, INT_MAX, totalValueInOfShard, vInputUtxoIdxToLock)) {
	std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
	return 'n';
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
    if (!CheckLockInputs(tx, vInputUtxoIdxToLock, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata, nullptr)) {  // do not use multithreads to check scripts
	std::cerr << __func__ << ": ConnectBlock(): CheckInputs on " 
		<< tx.GetHash().ToString() 
		<< " failed with " << FormatStateMessage(state)
		<< std::endl;
	return 'n';
    }

    /* Step 4: spent the input coins in our shard and store them in the global map 
     * for possibly future UnlockToAbort processing. */
    TxUndoInfo undoInfo;
    UpdateLockCoins(tx, vInputUtxoIdxToLock, view, undoInfo, seq);
    mapTxUndo.insert(std::make_pair(tx.GetHash(), undoInfo));
    /* -------------logic from Bitcoin code for tx processing--------- */
    std::cout << __func__ << ": locked input UTXOs for tx " << tx.GetHash().GetHex().substr(0, 10) << " at log slot " << seq << std::endl;
    return 'y';
}

uint256 LockReq::GetDigest() const {
    uint256 tx_hash(tx_mutable.GetHash());
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size());
    for (uint i = 0; i < nOutpointToLock; i++) {
	hasher .Write((const unsigned char*)&vInputUtxoIdxToLock[i], sizeof(vInputUtxoIdxToLock[i]));
    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

CInputShardReply::CInputShardReply(): CReply() {};

CInputShardReply::CInputShardReply(char replyIn, const uint256& digestIn, CAmount valueIn): 
    CReply(replyIn, digestIn), totalValueInOfShard(valueIn) {};

void CInputShardReply::getHash(uint256& result) const {
    uint256 tmp;
    CReply::getHash(tmp);
    CHash256().Write((const unsigned char*)tmp.begin(), tmp.size())
	    .Write((const unsigned char*)&totalValueInOfShard, sizeof(totalValueInOfShard))
	    .Finalize((unsigned char*)&result);
}

UnlockToCommitReq::UnlockToCommitReq(): CClientReq(CMutableTransaction()) {}
UnlockToCommitReq::UnlockToCommitReq(const CTransaction& txIn, const uint sigCountIn, std::vector<CInputShardReply>&& vReply) : CClientReq(txIn), nInputShardReplies(sigCountIn), vInputShardReply(vReply){}

uint256 UnlockToCommitReq::GetDigest() const {
    uint256 tx_hash(tx_mutable.GetHash());
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size());
    for (uint i = 0; i < nInputShardReplies; i++) {
	uint256 tmp;
	vInputShardReply[i].getHash(tmp);
	hasher.Write((const unsigned char*)tmp.begin(), tmp.size());

    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

bool checkInputShardReplySigs(const std::vector<CInputShardReply>& vReplies);

char UnlockToCommitReq::Execute(const int seq, CCoinsViewCache& view) const {
    if (!checkInputShardReplySigs(vInputShardReply)) {
        std::cout << __func__ << ": verify sigs fail!" << std::endl;
	return 'n';
    }

    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(tx_mutable);
    
    CValidationState state;
    unsigned int flags = SCRIPT_VERIFY_NONE; // only verify pay to public key hash

    /* Step 1: check the total input UTXO value is greater than the total output value.
     * We must use the locked UTXO value in InputShardReplies b/c we do not know the
     * value of UTXOs not in our shard. 
     * We use INT_MAX as block height, so that we never fail coinbase maturity check.
     */
    uint sigsPerInputShard = 2 * CPbft::nFaulty + 1;
    CAmount totalInputValue = 0;
    for (uint i = 0; i < vInputShardReply.size(); i += sigsPerInputShard) {
	totalInputValue += vInputShardReply[i].totalValueInOfShard;
    }

    if (!Consensus::CheckInputsCommitReq(tx, state, view, INT_MAX, totalInputValue)) {
	std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
	return 'n';
    }

    /* Step 2: count sig ops. Do this in the output shard. */
    // GetTransactionSigOpCost counts 3 types of sigops:
    // * legacy (always)
    // * p2sh (when P2SH enabled in flags and excludes coinbase)
    // * witness (when witness enabled in flags and excludes coinbase)
    int64_t nSigOpsCost = 0;
    nSigOpsCost += GetTransactionSigOpCostInOutShard(tx, view, flags);
    if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST) { 
	std::cerr << __func__ << ": ConnectBlock(): too many sigops" << std::endl;
	return 'n';
    }

    /* Step 3: check sigScript for input UTXOs in our shard. Done by input shard. */

    /* Step 4: 
     * 1) In input shard: spent the input coins in our shard and store them in the global map 
     * for possibly future UnlockToAbort processing. 
     * 2) In output shard: add output coins to coinsview.
     */
    UpdateUnlockCommitCoins(tx, view, seq);
    /* -------------logic from Bitcoin code for tx processing--------- */
    std::cout << __func__ << ":  commit tx " << tx.GetHash().GetHex().substr(0, 10) << " at log slot " << seq << std::endl;
    return 'y';
}

bool checkInputShardReplySigs(const std::vector<CInputShardReply>& vReplies) {
    // verify signature and return false if at least one sig is wrong
    for (auto reply: vReplies) {
	auto it = g_pbft->pubKeyMap.find(reply.peerID);
	if (it == g_pbft->pubKeyMap.end()) {
	    std::cerr << "no pub key for the sender" << reply.peerID << std::endl;
	    return false;
	}
	uint256 msgHash;
	reply.getHash(msgHash);
	if (!it->second.Verify(msgHash, reply.vchSig)) {
	    std::cerr << "verification sig fail" << std::endl;
	    return false;
	}
    }
    std::cout << __func__ << ": sig ok" << std::endl;
    return true;
}

UnlockToAbortReq::UnlockToAbortReq(): CClientReq(CMutableTransaction()) {
    vNegativeReply.resize(2 * CPbft::nFaulty + 1);
}

UnlockToAbortReq::UnlockToAbortReq(const CTransaction& txIn, const std::vector<CInputShardReply>& lockFailReplies) : CClientReq(txIn), vNegativeReply(lockFailReplies){
    assert(vNegativeReply.size() == 2 * CPbft::nFaulty + 1);
}

uint256 UnlockToAbortReq::GetDigest() const {
    uint256 tx_hash(tx_mutable.GetHash());
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size());
    for (uint i = 0; i < vNegativeReply.size(); i++) {
	uint256 tmp;
	vNegativeReply[i].getHash(tmp);
	hasher.Write((const unsigned char*)tmp.begin(), tmp.size());

    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

char UnlockToAbortReq::Execute(const int seq, CCoinsViewCache& view) const {
    if (!checkInputShardReplySigs(vNegativeReply)) {
        std::cout << __func__ << ": verify sigs fail!" << std::endl;
	return 'n';
    }

    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(tx_mutable);
    
    /* Step 1: check all replies are negative. */
    for (auto r: vNegativeReply) {
	assert(r.reply == 'n');
    }

    /* Step 2: 
     * add input UTXOs in our shard back to the coinsview.
     * The client sends an abort req to all input shards, so an input shard should
     * check if it has locked some UTXOs (in the case that this shard vote abort,
     * it must have failed to lock UTXOs). The input shard should only restore
     * UTXOs if it has locked them.
     */
    if (mapTxUndo.find(tx.GetHash()) != mapTxUndo.end()) {
	std::cout << __func__ << ":  abort tx " << tx.GetHash().GetHex().substr(0, 10) << " at log slot " << seq << ", restoring locked UTXOs."<< std::endl;
	UpdateUnlockAbortCoins(tx, view, mapTxUndo[tx.GetHash()]);
    } else {
	std::cout << __func__ << ":  abort tx " << tx.GetHash().GetHex().substr(0, 10) << " at log slot " << seq << ", no UTXO to restore."<< std::endl;
    }
    /* ------------- end logic from Bitcoin code for tx processing--------- */

    return 'y';
}

CPbftBlock::CPbftBlock(){
    hash.SetNull();
    vReq.clear();
}

CPbftBlock::CPbftBlock(std::deque<TypedReq> vReqIn) {
    hash.SetNull();
    vReq.insert(vReq.end(), vReqIn.begin(), vReqIn.end());
}

void CPbftBlock::ComputeHash(){
    CHash256 hasher;
    for (uint i = 0; i < vReq.size(); i++) {
	hasher.Write((const unsigned char*)vReq[i].pReq->GetHash().begin(), vReq[i].pReq->GetHash().size());
    }
    hasher.Finalize((unsigned char*)&hash);
}

uint32_t CPbftBlock::Execute(const int seq, CConnman* connman) const {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    uint32_t txCnt = 0;
    CCoinsViewCache view(pcoinsTip.get());
    for (uint i = 0; i < vReq.size(); i++) {
	char exe_res = vReq[i].pReq->Execute(seq, view);
	if (vReq[i].type == ClientReqType::LOCK) {
	    CInputShardReply reply = g_pbft->assembleInputShardReply(seq, i, exe_res, ((LockReq*)vReq[i].pReq.get())->totalValueInOfShard);
	    connman->PushMessage(g_pbft->client, msgMaker.Make(NetMsgType::OMNI_LOCK_REPLY, reply));
	} else {
	    CReply reply = g_pbft->assembleReply(seq, i, exe_res);
	    connman->PushMessage(g_pbft->client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
	    if (vReq[i].type == ClientReqType::TX || vReq[i].type == ClientReqType::UNLOCK_TO_COMMIT) {
		/* only count TX and UNLOCK_TO_COMMIT requests */
		txCnt++;
	    }
	}
    }
    bool flushed = view.Flush(); // flush to pcoinsTip
    assert(flushed);
    return txCnt;
}

uint256 TypedReq::GetHash() const {
    uint256 req_hash(pReq->GetDigest());
    uint256 result;
    CHash256().Write((const unsigned char*)req_hash.begin(), req_hash.size()).Write((const unsigned char*)type, sizeof(type)).Finalize((unsigned char*)&result);
    return result;
}

