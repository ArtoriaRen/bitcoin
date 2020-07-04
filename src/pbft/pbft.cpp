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
#include <memory>
#include "tx_placement/tx_placer.h"

bool fIsClient; // if this node is a pbft client.
std::string leaderAddrString;
std::string clientAddrString;
int32_t pbftID; 

CPbft::CPbft() : localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastExecutedSeq(-1), client(nullptr), peers(std::vector<CNode*>(groupSize * num_committees)), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    pubKeyMap.insert(std::make_pair(pbftID, myPubKey));
}


bool CPbft::ProcessPP(CConnman* connman, CPre_prepare& ppMsg) {
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    std::cout << __func__ << ": req type = " <<  ppMsg.type << std::endl;
    if (!checkMsg(&ppMsg)) {
        return false;
    }

    // check if the digest matches client req
    if (ppMsg.digest != ppMsg.req->GetDigest()) {
	std::cerr << "digest does not match client tx. Client txid = " << ppMsg.req->GetDigest().GetHex() << ", but digest = " << ppMsg.digest.GetHex() << std::endl;
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
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    uint32_t start_peerID = pbftID - pbftID % groupSize;
    uint32_t end_peerID = start_peerID + groupSize;
    for (uint32_t i = start_peerID; i < end_peerID; i++) {
	/* do not send a msg to myself. */
	if (i == pbftID)
	    continue; 
	connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::PBFT_P, pMsg));
    }
    /* add the pMsg to our own log.
     * We should do this after we send the pMsg to other peers so that we always 
     * send pMsg before cMsg. 
     */
    ProcessP(connman, pMsg, false);
    return true;
}

bool CPbft::ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(&pMsg)) {
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
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	uint32_t start_peerID = pbftID - pbftID % groupSize;
	uint32_t end_peerID = start_peerID + groupSize;
	for (uint32_t i = start_peerID; i < end_peerID; i++) {
	    /* do not send a msg to myself. */
	    if (i == pbftID)
		continue; 
	    connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::PBFT_C, cMsg));
	}
	/* add the cMsg to our own log.
	 * We should do this after we send the pMsg to other peers so that we always 
	 * send cMsg before execute tx. 
	 */
	ProcessC(connman, cMsg, false);
    }
    return true;
}

bool CPbft::ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(&cMsg)) {
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
	/* if some seq ahead of the cMsg.seq is not in the reply phase yet, 
	 * cMsg.seq will not be executed.
	 */
	int startReplySeq = lastExecutedSeq + 1; 
	executeTransaction(cMsg.seq); // this updates the lastExecutedIndex  
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	/* send reply to for all log slots that are in the reply phase.
	 * Since lastExecutedSeq increaes monotonically, this logic guarantees 
	 * to send reply to client only once. 
	 */
	for (int i = startReplySeq; i <= lastExecutedSeq; i++) {
	    if (log[cMsg.seq].ppMsg.type == ClientReqType::TX || 
		    log[cMsg.seq].ppMsg.type == ClientReqType::UNLOCK_TO_COMMIT) {
		CReply reply = assembleReply(cMsg.seq);
		connman->PushMessage(client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
	    } else if (log[cMsg.seq].ppMsg.type == ClientReqType::LOCK) {
		CInputShardReply reply = assembleInputShardReply(cMsg.seq);
		connman->PushMessage(client, msgMaker.Make(NetMsgType::OMNI_LOCK_REPLY, reply));
	    } 
	}
    }
    return true;
}

bool CPbft::checkMsg(CPbftMessage* msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(msg->peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << msg->peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    msg->getHash(msgHash);
    if (!it->second.Verify(msgHash, msg->vchSig)) {
        std::cerr << "verification sig fail" << std::endl;
        return false;
    }
    std::cout << __func__ << ": sig ok" << std::endl;
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

CPre_prepare CPbft::assemblePPMsg(const std::shared_ptr<CClientReq>& pclientReq, ClientReqType type) {
    CPre_prepare toSent; // phase is set to Pre_prepare by default.
    toSent.seq = nextSeq++;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.type = type;
    toSent.req = pclientReq;
    toSent.digest = toSent.req->GetDigest();
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so client tx is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CPbftMessage CPbft::assembleMsg(const uint32_t seq) {
    CPbftMessage toSent(log[seq].ppMsg);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CReply CPbft::assembleReply(const uint32_t seq) {
    /* 'y' --- execute sucessfully
     * 'n' --- execute fail
     */
    CReply toSent(log[seq].result, log[seq].ppMsg.digest);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CInputShardReply CPbft::assembleInputShardReply(const uint32_t seq) {
    CInputShardReply toSent('y', log[seq].ppMsg.digest, ((LockReq*)log[seq].ppMsg.req.get())->totalValueInOfShard);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

int CPbft::executeTransaction(const int seq) {
    // execute all lower-seq tx until this one if possible.
    int i = lastExecutedSeq + 1;
    /* We should go on to execute all log slots that are in reply phase even
     * their seqs are greater than the seq passed in. If we only execute up to
     * the seq passed in, a slot missing a pbftc msg might permanently block
     * log slots after it to be executed. */
    for (; i < logSize; i++) {
        if (log[i].phase == PbftPhase::reply) {
	    log[i].result = log[i].ppMsg.req->Execute(i);
        } else {
            break;
        }
    }
    lastExecutedSeq = i - 1;
    /* if lastExecutedIndex is less than seq, we delay sending reply until 
     * the all requsts up to seq has been executed. This may be triggered 
     * by future requests.
     */
    return lastExecutedSeq;
}


std::unique_ptr<CPbft> g_pbft;
/* In case we receive an omniledger unlock_to_abort req, store a copy of all locked coins
 * so that they can be added back to pcoinsTip. */
std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> lockedCoinMap;