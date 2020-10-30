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

CPre_prepare::CPre_prepare(const CPre_prepare& msg): CPbftMessage(msg), pbft_block(msg.pbft_block) { }

CPre_prepare::CPre_prepare(const CPbftMessage& msg): CPbftMessage(msg) { }



static char VerifyTx(CMutableTxRef tx_mutable, const int seq, CCoinsViewCache& view) {
    /* -------------logic from Bitcoin code for tx processing--------- */
    CTransaction tx(*tx_mutable);
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
    return 'y';
}

static char ExecuteTx(CMutableTxRef tx_mutable, const int seq, CCoinsViewCache& view) {
    CTransaction tx(*tx_mutable);
//	    CTxUndo undoDummy;
//	    if (i > 0) {
//		blockundo.vtxundo.push_back(CTxUndo());
//	    }
    UpdateCoins(tx, view, seq);
    /* -------------logic from Bitcoin code for tx processing--------- */

    //std::cout << __func__ << ": excuted tx " << tx.GetHash().ToString()
//	    << " at log slot " << seq << std::endl;
    return 'y';
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

CPbftBlock::CPbftBlock(std::deque<CMutableTxRef> vReqIn) {
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

uint32_t CPbftBlock::Verify(const int seq, CCoinsViewCache& view, bool amidExecution, CConnman* connman) const {
    uint32_t txCnt = 0;
    bool isInOurSubgroup = g_pbft->isBlockInOurVerifyGroup(seq);
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    if (amidExecution) {
	assert(connman != nullptr);
	for (uint i = 0; i < vReq.size(); i++) {
	    char exe_res = VerifyTx(vReq[i], seq, view);
	    txCnt++;
	}
    } else {
	for (uint i = 0; i < vReq.size(); i++) {
	    VerifyTx(vReq[i], seq, view);
	    txCnt++;
	    if (!isInOurSubgroup && g_pbft->log[seq].blockVerified.load(std::memory_order_relaxed)) {
	        /* enough collab msg is received. */
	        std::cout << "quit verifying block " << seq << " of the other subgroup" << std::endl;
	        break;
	    }     
	}
    }
    return txCnt;
}

uint32_t CPbftBlock::Execute(const int seq, CConnman* connman, CCoinsViewCache& view) const {
    if (!connman) {
        /* this is for tentative execution. */
        for (uint i = 0; i < vReq.size(); i++) {
            ExecuteTx(vReq[i], seq, view);
        }
        return vReq.size();
    }

    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    uint32_t txCnt = 0;
    for (uint i = 0; i < vReq.size(); i++) {
	char exe_res = ExecuteTx(vReq[i], seq, view);
        CReply reply = g_pbft->assembleReply(seq, i, exe_res);
        connman->PushMessage(g_pbft->client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
        txCnt++;
    }

    return txCnt;
}

CCollabMessage::CCollabMessage(): peerID(pbftID), blockValidUpto(-1), sigSize(0), vchSig(){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CCollabMessage::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&blockValidUpto, sizeof(blockValidUpto))
	    .Finalize((unsigned char*)&result);
}
