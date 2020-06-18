/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <locale>
#include<string.h>
#include <netinet/in.h>
#include "pbft/pbft.h"
#include "init.h"
#include "validation.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "consensus/validation.h"
#include "consensus/params.h"
#include "consensus/tx_verify.h"
#include "coins.h"
#include "script/interpreter.h"
#include "undo.h"
#include "netmessagemaker.h"

bool fIsClient; // if this node is a pbft client.
std::string leaderAddrString;
std::string clientAddrString;

CPbft::CPbft() : groupSize(4), nFaulty(1), localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastExecutedIndex(-1), leader(nullptr), client(nullptr), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
}


bool CPbft::ProcessPP(CNode* pfrom, CConnman* connman, CPre_prepare& ppMsg) {
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if (!checkMsg(pfrom, &ppMsg)) {
        return false;
    }

    // check if the digest matches client req
    if (ppMsg.digest != ppMsg.tx.GetHash()) {
	std::cerr << "digest does not match client tx. Client txid = " << ppMsg.tx.GetHash().GetHex() << ", but digest = " << ppMsg.digest.GetHex() << std::endl;
	return false;
    }
    // add to log
    log[ppMsg.seq].ppMsg = ppMsg;
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */

    std::cout << "digest = " << ppMsg.digest.GetHex() << std::endl;

    /* Enter prepare phase. add the pMsg of from the node itself. */
    log[ppMsg.seq].phase = PbftPhase::prepare;
    /* make a pMsg */
    CPbftMessage pMsg = assembleMsg(ppMsg.seq);
    /* send the pMsg to other peers, including the leader. */
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman->PushMessage(leader, msgMaker.Make(NetMsgType::PBFT_P, pMsg)); // the leader will not receive ppMs, so we do not check if we are the leader here.
    for (CNode* node: otherMembers) {
	connman->PushMessage(node, msgMaker.Make(NetMsgType::PBFT_P, pMsg));
    }
    /* add the pMsg to our own log.
     * We should do this after we send the pMsg to other peers so that we always 
     * send pMsg before cMsg. 
     */
    ProcessP(nullptr, connman, pMsg, false);
    return true;
}

bool CPbft::ProcessP(CNode* pfrom, CConnman* connman, CPbftMessage& pMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(pfrom, &pMsg)) {
	    return false;
	}

	// check the message's view mactches the ppMsg's view in log.
	if (log[pMsg.seq].ppMsg.view != pMsg.view) {
	    std::cerr << "log entry view = " << log[pMsg.seq].ppMsg.view 
		    << ", but msg view = " << pMsg.view << std::endl;
	    return false;
	}
    }

    /* add to log (currently use placeholder: should add the entire message 
     * to log and not increase re-count if the sender is the same. 
     * Also, if prepares are received earlier than pre-prepare, different
     * prepare may have different digest. Should categorize buffered prepares 
     * based on digest.)
     */
    // log[prepare.seq].prepareArray.push_back(prepare);

    /* count the number of prepare msg. enter commit if greater than 2f */
    log[pMsg.seq].prepareCount++;
    /* In the if condition, we use == (nFaulty << 1) instead of >= (nFaulty << 1),
     * so that we do not re-send commit msg every time another prepare msg is received.
     */
    if (log[pMsg.seq].phase == PbftPhase::prepare && log[pMsg.seq].prepareCount == (nFaulty << 1)) {
	/* Enter commit phase. add the cMsg of from the node itself. */
        log[pMsg.seq].phase = PbftPhase::commit;
	log[pMsg.seq].commitCount++;
	/* make a cMsg */
	CPbftMessage cMsg = assembleMsg(pMsg.seq);
	/* send the cMsg to other peers */
	const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
	if(leader != nullptr) {
	    /* Only followers send cMsg to the leader. The leader should not
	     * send cMsg to itself.
	     * The leader has pbft->leader == nullptr b/c no other nodes with
	     * the leader's address connects to the leader as a peer. 
	     */
	    std::cout << __func__ << ": leader->GetAddrName() = " 
		    << leader->GetAddrName()
		    << ",  leaderAddrString = " <<  leaderAddrString
		    << std::endl;
	    connman->PushMessage(leader, msgMaker.Make(NetMsgType::PBFT_C, cMsg));
	}
	for (CNode* node: otherMembers) {
	    connman->PushMessage(node, msgMaker.Make(NetMsgType::PBFT_C, cMsg));
	}
	/* add the cMsg to our own log.
	 * We should do this after we send the pMsg to other peers so that we always 
	 * send cMsg before execute tx. 
	 */
	ProcessC(nullptr, connman, cMsg, false);
    }
    return true;
}

bool CPbft::ProcessC(CNode* pfrom, CConnman* connman, CPbftMessage& cMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(pfrom, &cMsg)) {
	    return false;
	}

	// check the message's view mactches the ppMsg's view in log.
	if (log[cMsg.seq].ppMsg.view != cMsg.view) {
	    std::cerr << "log entry view = " << log[cMsg.seq].ppMsg.view 
		    << ", but msg view = " << cMsg.view << std::endl;
	    return false;
	}
    }

    // count the number of commit msg. enter execute if greater than 2f+1
    log[cMsg.seq].commitCount++;
    if (log[cMsg.seq].phase == PbftPhase::commit && log[cMsg.seq].commitCount == (nFaulty << 1) + 1) {
        // enter reply phase
        std::cout << "enter reply phase" << std::endl;
        log[cMsg.seq].phase = PbftPhase::reply;
        executeTransaction(cMsg.seq);
	/* send reply to client only once. */
	CReply cReply = assembleReply(cMsg.seq);
	const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
	connman->PushMessage(client, msgMaker.Make(NetMsgType::PBFT_REPLY, cReply));
    }
    return true;
}

bool CPbft::checkMsg(CNode* pfrom, CPbftMessage* msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(pfrom->addr.ToStringIPPort());
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for the sender" << std::endl;
        return false;
    }
    uint256 msgHash;
    msg->getHash(msgHash);
    if (!it->second.Verify(msgHash, msg->vchSig)) {
        std::cerr << "verification sig fail" << std::endl;
        return false;
    }
    std::cerr << "sig ok" << std::endl;
    // server should be in the view
    if (localView != msg->view) {
        std::cerr << "server view = " << localView << ", but msg view = " << msg->view << std::endl;
        return false;
    }

    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if (!log[msg->seq].ppMsg.digest.IsNull() && log[msg->seq].ppMsg.digest != msg->digest) {
        std::cerr << "digest error for log entry " << msg->seq << ". digest in log = " << log[msg->seq].ppMsg.digest.GetHex() << ", but msg->digest = " << msg->digest.GetHex() << std::endl;
        return false;
    }


    return true;
}

CPre_prepare CPbft::assemblePPMsg(const CTransaction& tx) {
#ifdef MSG_ASSEMBLE
    std::cout << "assembling pre_prepare, seq = " << seq << "client req = " << clientReq << std::endl;
#endif
    CPre_prepare toSent; // phase is set to Pre_prepare by default.
    toSent.seq = nextSeq++;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = tx.GetHash();
    toSent.tx = CMutableTransaction(tx);
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so client tx is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CPbftMessage CPbft::assembleMsg(uint32_t seq) {
    CPbftMessage toSent(log[seq].ppMsg);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CReply CPbft::assembleReply(uint32_t seq) {
    /* 'y' --- execute sucessfully
     * 'n' --- execute fail
     * Currently we only reply with 'y' b/c servers will exit when an error occurs.
     */
    CReply toSent('y', log[seq].ppMsg.digest);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

void CPbft::executeTransaction(const int seq) {
    // execute all lower-seq tx until this one if possible.
    int i = lastExecutedIndex + 1;
    for (; i < seq + 1; i++) {
        if (log[i].phase == PbftPhase::reply) {
            /* client request message format: "r <request type>,<key>[,<value>]". 
             * Request type can be either read 'r' or write 'w'. If it is a 
             * write request, client must provide value. We use comma as delimiter 
             * here to avoid coflict with message deserialization, which use space
             * as delimiter.
             */
	    const CTransaction &tx = log[i].ppMsg.tx;
	    
	    /* -------------logic from Bitcoin code for tx processing--------- */
	    CValidationState state;
	    CCoinsViewCache view(pcoinsTip.get());
	    std::vector<PrecomputedTransactionData> txdata;
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
            if (!MoneyRange(txfee)) {
		std::cerr << __func__ << ": accumulated fee in the block out of range." << std::endl;
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

	    txdata.emplace_back(tx);
	    std::vector<CScriptCheck> vChecks;
	    bool fCacheResults = false; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
	    if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata[i], nullptr)) {  // do not use multithreads to check scripts
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

	    std::cout << __func__ << "excuted tx " << tx.GetHash().ToString()
		    << " at log slot " << i << std::endl;
            /* send reply right after execution. */
//            sendReply2Client(i);
        } else {
            break;
        }
    }
    lastExecutedIndex = i - 1;
    /* if lastExecutedIndex is less than seq, we delay sending reply until 
     * the all requsts up to seq has been executed. This may be triggered 
     * by future requests.
     */
}
//
//void CPbft::sendReply2Client(const int seq) {
//    std::ostringstream oss;
//    CReply r = assembleReply(seq);
//    r.serialize(oss);
//    // hard code client address for now.
//    std::string clientIP = "127.0.0.1";
//    int clientUdpPort = 12345;
//    udpClient.sendto(oss, clientIP, clientUdpPort);
//#ifdef REPLY
//    std::cout << "have sent reply to client port " << clientUdpPort << std::endl;
//#endif
//}
//
//void CPbft::broadcastPubKey() {
//    std::ostringstream oss;
//    // opti: serialized version can be stored.
//    serializePubKeyMsg(oss, server_id, udpServer->get_port(), publicKey);
//    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
//    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
//}
//
//void CPbft::sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id) {
//    std::ostringstream oss;
//    serializePubKeyMsg(oss, server_id, udpServer->get_port(), publicKey);
//    udpClient.sendto(oss, inet_ntoa(src_addr.sin_addr), peers.at(recver_id).port);
//}
//
//void CPbft::broadcastPubKeyReq() {
//    std::ostringstream oss;
//    oss << pubKeyReqHeader;
//    oss << " ";
//    oss << server_id;
//    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
//    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
//}
//
//CPubKey CPbft::getPublicKey() {
//    return publicKey;
//}


std::unique_ptr<CPbft> g_pbft;