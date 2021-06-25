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

bool VerifyTx(const CTransaction& tx, const int seq, CCoinsViewCache& view) {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CValidationState state;
    if(!tx.IsCoinBase()) {
        bool fScriptChecks = true;
    //	    CBlockUndo blockundo;
        unsigned int flags = SCRIPT_VERIFY_NONE | SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS; // only verify pay to public key hash
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

bool VerifyButNoExecuteTx(const CTransaction& tx, const int seq, CCoinsViewCache& view) {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CValidationState state;
    if(!tx.IsCoinBase()) {
        bool fScriptChecks = true;
    //	    CBlockUndo blockundo;
        unsigned int flags = SCRIPT_VERIFY_NONE | SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS; // only verify pay to public key hash
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
    return true;
}

bool ExecuteTx(const CTransaction& tx, const int seq, CCoinsViewCache& view) {
    /* check for missing inputs before update the system state. */
    CValidationState state;
    CAmount txfee = 0;
    if(!tx.IsCoinBase()) {
        if (!Consensus::CheckTxInputs(tx, state, view, INT_MAX, txfee)) {
            std::cerr << __func__ << ": Consensus::CheckTxInputs: " << tx.GetHash().ToString() << ", " << FormatStateMessage(state) << std::endl;
            return false;
        }
    }
    UpdateCoins(tx, view, seq);
    return true;
}

CReply::CReply(): reply(), peerID(pbftID), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CReply::CReply(char replyIn, std::deque<uint256>&& vTxIn): reply(replyIn), vTx(vTxIn.begin(), vTxIn.end()), peerID(pbftID), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CReply::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&reply, sizeof(reply))
	    .Write((const unsigned char*)vTx.data(), vTx.size() * vTx[0].size())
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

uint32_t CPbftBlock::Verify(const int seq, CCoinsViewCache& view) const {
    std::vector<uint32_t> validTxs; 
    std::deque<uint32_t> invalidTxs;
    uint32_t validTxCnt = 0;
    struct timeval start_time, end_time;
    struct timeval totalVrfTime = {0, 0};
    for (uint i = 0; i < vReq.size(); i++) {
        gettimeofday(&start_time, NULL);
        bool isValid = VerifyTx(*vReq[i], seq, view);
        //std::cout << " verified tx " << vReq[i]->GetHash().ToString() << "of block " << seq << std::endl;
        gettimeofday(&end_time, NULL);
        totalVrfTime += end_time - start_time;
        if (isValid) {
            //std::cout << __func__ << ": tx (" << seq << ", "  << i << ") " << vReq[i]->GetHash().ToString().substr(0, 10) << " is valid" << std::endl;
            validTxs.push_back(i); 
            validTxCnt++;
        } else {
            invalidTxs.push_back(i);
        }
        if(validTxs.size() == vrfResBatchSize  || i == vReq.size() - 1) {
            /* do no count invalid tx. our goal here is to only count how many tx
             * will be executed by simply sharing vrf results without tracking dependency. */
            /* processed enough tx, move them to the global queue. */
            //std::cout << "verified to tx "  << i << " in block " << seq << ". valid tx cnt = " << validTxs.size() << std::endl;
            std::vector<uint32_t> dummy;
            g_pbft->Copy2CollabMsgQ(seq, vReq.size(), validTxs, dummy);
            validTxs.clear();
        }
    }

    if (validTxCnt > 0) {
        std::cout << "Average verify time of block " << seq << ": " << (totalVrfTime.tv_sec * 1000000 + totalVrfTime.tv_usec) / (validTxCnt) << " us/req";
    } else {
        std::cout << "Average verify time of block " << seq << ": all tx are invalid";
    }
    std::cout << ". valid tx cnt = " << validTxCnt << ". invalid tx cnt = " << invalidTxs.size() << std::endl;
    /* inform the reply sending thread of what tx is not executed. 
     * We cannot include invalid tx because all reply are hard-coded to 
     * positive execution results.
     */
    g_pbft->informReplySendingThread(seq, invalidTxs);
    return validTxCnt;
}

uint32_t CPbftBlock::Execute(const int seq, CCoinsViewCache& view) const {
    /* this is for tentative execution. */
    for (uint i = 0; i < vReq.size(); i++) {
	ExecuteTx(*vReq[i], seq, view);
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

CCollabMessage::CCollabMessage(uint32_t heightIn, uint32_t txCntIn, std::vector<uint32_t>& validTxsIn, std::vector<uint32_t>& invalidTxsIn): height(heightIn), txCnt(txCntIn), validTxs(validTxsIn),invalidTxs(invalidTxsIn), peerID(pbftID), sigSize(0), vchSig() {
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CCollabMessage::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&height, sizeof(height))
            .Write((const unsigned char*)&txCnt, sizeof(txCnt))
            .Write((const unsigned char*)validTxs.data(), validTxs.size() * sizeof(uint32_t))
            .Write((const unsigned char*)invalidTxs.data(), invalidTxs.size() * sizeof(uint32_t))
	    .Finalize((unsigned char*)&result);
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

CBlockMsg::CBlockMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn, uint32_t seq): logSlot(seq), pPbftBlock(pPbftBlockIn), peerID(pbftID) { }

void CBlockMsg::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&logSlot, sizeof(logSlot))
            .Write((const unsigned char*)pPbftBlock->hash.begin(), pPbftBlock->hash.size())
	    .Finalize((unsigned char*)&result);
}
