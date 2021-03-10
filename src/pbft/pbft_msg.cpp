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
#include "coins.h"
#include "script/interpreter.h"
#include "consensus/params.h"
#include "undo.h"
#include "consensus/tx_verify.h"
#include "netmessagemaker.h"

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

CPre_prepare::CPre_prepare(): CPbftMessage(), pPbftBlock() { }

CPre_prepare::CPre_prepare(const CPre_prepare& msg): CPbftMessage(msg), pPbftBlock(msg.pPbftBlock) { }

CPre_prepare::CPre_prepare(const CPbftMessage& msg): CPbftMessage(msg), pPbftBlock() { }



bool VerifyTx(const CTransaction& tx, const int seq, CCoinsViewCache& view) {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CValidationState state;
    if(!tx.IsCoinBase()) {
        bool fScriptChecks = true;
    //	    CBlockUndo blockundo;
        unsigned int flags = SCRIPT_VERIFY_NONE; // only verify pay to public key hash
        CAmount txfee = 0;
        /* We use  INT_MAX as block height, so that we never fail coinbase 
         * maturity check. */
        if (!Consensus::CheckTxInputs(tx, state, view, INT_MAX, txfee)) {
            std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
            return false;
        }

        // GetTransactionSigOpCost counts 3 types of sigops:
        // * legacy (always)
        // * p2sh (when P2SH enabled in flags and excludes coinbase)
        // * witness (when witness enabled in flags and excludes coinbase)
        int64_t nSigOpsCost = 0;
        nSigOpsCost += GetTransactionSigOpCost(tx, view, flags);
        if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST) { 
            std::cerr << __func__ << ": ConnectBlock(): too many sigops" << std::endl;
            return false;
        }

        PrecomputedTransactionData txdata(tx);
        std::vector<CScriptCheck> vChecks;
        bool fCacheResults = false; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
        if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata, nullptr)) {  // do not use multithreads to check scripts
            std::cerr << __func__ << ": ConnectBlock(): CheckInputs on " 
                    << tx.GetHash().ToString() 
                    << " failed with " << FormatStateMessage(state)
                    << std::endl;
            return false;
        }
    }
    UpdateCoins(tx, view, seq);
    return true;
}

bool ExecuteTx(const CTransaction& tx, const int seq, CCoinsViewCache& view) {
    UpdateCoins(tx, view, seq);
    return true;
}

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

CPbftBlock::CPbftBlock(){
    hash.SetNull();
    vReq.clear();
}

CPbftBlock::CPbftBlock(std::deque<CTransactionRef> vReqIn) {
    hash.SetNull();
    vReq.insert(vReq.end(), vReqIn.begin(), vReqIn.end());
}

void CPbftBlock::ComputeHash(){
    CHash256 hasher;
    for (uint i = 0; i < vReq.size(); i++) {
	hasher.Write((const unsigned char*)vReq[i]->GetHash().begin(), vReq[i]->GetHash().size());
    }
    hasher.Finalize((unsigned char*)&hash);
}

/* check if the tx have create-spend or spend-spend dependency with tx not yet verified. */
static bool havePrereqTx(uint32_t height, uint32_t txSeq) {
    CPbft& pbft= *g_pbft;
    CTransactionRef tx = pbft.log[height].ppMsg.pPbftBlock->vReq[txSeq];
    std::unordered_set<uint256, uint256Hasher> preReqTxs;
    for (const CTxIn& inputUtxo: tx->vin) {
        /* check create-spend dependency. */
        if (pbft.mapTxDependency.find(inputUtxo.prevout.hash) != pbft.mapTxDependency.end()) {
            preReqTxs.emplace(inputUtxo.prevout.hash);
        }
        /* check spend-spend dependency. */
        if (pbft.mapUtxoConflict.find(inputUtxo.prevout) != pbft.mapUtxoConflict.end()) {
            for (uint256& conflictTx: pbft.mapUtxoConflict[inputUtxo.prevout]) {
                 preReqTxs.emplace(conflictTx);
            }
            /* add this tx to de UTXO spending list so that future tx spending this UTXO
             * know this tx is a prerequisite tx for it. */
            pbft.mapUtxoConflict[inputUtxo.prevout].emplace_back(tx->GetHash());
        }
    }

    if (preReqTxs.size() != 0) {
        /* add this tx as a dependent tx to the dependency graph. */
        for (const uint256& prereqTx: preReqTxs) {
            pbft.mapTxDependency[prereqTx].emplace_back(height, txSeq);
        }
        pbft.mapPrereqCnt[TxIndexOnChain(height, txSeq)] = preReqTxs.size();
        /* add this tx as a potential prereqTx to the dependency graph. */
        pbft.mapTxDependency.emplace(tx->GetHash(), std::list<TxIndexOnChain>()); 
        for (const CTxIn& inputUtxo: tx->vin) {
            if (pbft.mapUtxoConflict.find(inputUtxo.prevout) == pbft.mapUtxoConflict.end()) {
                pbft.mapUtxoConflict.emplace(inputUtxo.prevout, std::list<uint256>(1, tx->GetHash())); // create an entry for this UTXO and put this tx in the list
            }
        }
        return true;
    }
    return false;

}

static bool sendReplies(const uint32_t height, const uint32_t tx_seq, const char res) {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    const CPbft& pbft = *g_pbft;
    CReply reply = pbft.assembleReply(height, tx_seq, res);
    g_connman->PushMessage(pbft.client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
    return true;
}

uint32_t CPbftBlock::Verify(const int seq, CCoinsViewCache& view, std::vector<char>& validTxs, std::vector<uint32_t>& invalidTxs) const {
    uint32_t validTxCnt = 0;
    for (uint i = 0; i < vReq.size(); i++) {
        /* the block is not yet collab verified. We have to verify tx.*/
        if (havePrereqTx(seq, i))
            continue;
        if (VerifyTx(*vReq[i], seq, view)) {
            char bit = 1 << (i % 8); 
            validTxs[i >> 3] |= bit; 
            validTxCnt++;
            /* Send reply to client */
            sendReplies(seq, i, 'y');
        } else {
            invalidTxs.push_back(i);
            /* Send reply to client */
            sendReplies(seq, i, 'n');
        }
    }
    return validTxCnt;
}

uint32_t CPbftBlock::Execute(const int seq, CCoinsViewCache& view) const {
    /* this is for tentative execution. */
    for (uint i = 0; i < vReq.size(); i++) {
	ExecuteTx(*vReq[i], seq, view);
        /* Send reply to client */
        sendReplies(seq, i, 'y');
    }
    return vReq.size();
}

void CPbftBlock::Clear() {
    hash.SetNull();
    vReq.clear();
}

CCollabMessage::CCollabMessage(): peerID(pbftID), sigSize(0), vchSig() {
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CCollabMessage::CCollabMessage(uint32_t heightIn, std::vector<char>&& validTxsIn, std::vector<uint32_t>&& invalidTxsIn): height(heightIn), validTxs(validTxsIn),invalidTxs(invalidTxsIn), peerID(pbftID), sigSize(0), vchSig() {
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CCollabMessage::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&height, sizeof(height))
            .Write((const unsigned char*)validTxs.data(), validTxs.size())
            .Write((const unsigned char*)invalidTxs.data(), invalidTxs.size() * sizeof(uint32_t))
	    .Finalize((unsigned char*)&result);
}

/* fetch the bit for this tx. If the bit is 1, then the tx is valid. */
bool CCollabMessage::isValidTx(const uint32_t txSeq) const {
    char bit_mask = 1 << (txSeq % 8);
    return  validTxs[txSeq >> 3] & bit_mask;
}

CCollabMultiBlockMsg::CCollabMultiBlockMsg(): peerID(pbftID), sigSize(0), vchSig() {
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CCollabMultiBlockMsg::CCollabMultiBlockMsg(std::vector<TxIndexOnChain>&& validTxsIn, std::vector<TxIndexOnChain>&& invalidTxsIn): validTxs(validTxsIn),invalidTxs(invalidTxsIn), peerID(pbftID), sigSize(0), vchSig() {
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CCollabMultiBlockMsg::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)validTxs.data(), validTxs.size())
            .Write((const unsigned char*)invalidTxs.data(), invalidTxs.size() * sizeof(uint32_t))
	    .Finalize((unsigned char*)&result);
}

void CCollabMultiBlockMsg::clear() {
    validTxs.clear();
    invalidTxs.clear();
}

bool CCollabMultiBlockMsg::empty() const {
    return validTxs.empty() && invalidTxs.empty();
}
