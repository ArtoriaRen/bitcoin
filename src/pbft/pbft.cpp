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
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"

bool fIsClient; // if this node is a pbft client.

CPbft::CPbft() : groupSize(4), localView(0), nextSeq(0), lastExecutedIndex(-1), privateKey(CKey()){
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
}

//CPbft& CPbft::operator=(const CPbft& rhs) {
//    if (this == &rhs)
//        return *this;
//    localView = rhs.localView;
//    log = rhs.log;
//    nextSeq = rhs.nextSeq;
//    lastExecutedIndex = rhs.lastExecutedIndex;
//    leader = rhs.leader;
//    members = rhs.members;
//    server_id = rhs.server_id;
//    peers = rhs.peers;
//    udpServer = rhs.udpServer;
//    udpClient = rhs.udpClient;
//    pRecvBuf = rhs.pRecvBuf;
//    privateKey = rhs.privateKey;
//    publicKey = rhs.publicKey;
//    data = rhs.data;
//    return *this;
//}

/**
 * Go through the last nBlocks block, calculate membership of nGroups groups.
 * @param random is the random number used to group nodes.
 * @param nBlocks is number of blocks whose miner participate in the PBFT.
 * @return 
 */
//void CPbft::group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindexNew) {
//    const CBlockIndex* pindex = pindexNew; // make a copy so that we do not change the original argument passed in
//    LogPrintf("group number %d nBlock = %d, pindex->nHeight = %d \n", nGroups, nBlocks, pindex->nHeight);
//    for (uint i = 0; i < nBlocks && pindex != nullptr; i++) {
//        //TODO: get block miner IP addr and port, add them to the members
//        LogPrintf("pbft: block height = %d, ip&port = %s \n ", pindex->nHeight, pindex->netAddrPort.ToString());
//        pindex = pindex->pprev;
//    }
//
//}

//void interruptableReceive(CPbft& pbftObj) {
//    // Placeholder: broadcast myself pubkey, and request others' pubkey.
//    pbftObj.broadcastPubKey();
//    pbftObj.broadcastPubKeyReq(); // request peer publickey
//    struct sockaddr_in src_addr; // use this stuct to get sender IP and port
//    size_t len = sizeof (src_addr);
//
//    while (!ShutdownRequested()) {
//        // timeout block on receving a new packet. Attention: timeout is in milliseconds. 
//        ssize_t recvBytes = pbftObj.udpServer->timed_recv(pbftObj.pRecvBuf.get(), CPbftMessage::messageSizeBytes, 500, &src_addr, &len);
//
//        if (recvBytes == -1) {
//            // timeout. but we have got peer publickey. do nothing.
//            continue;
//        }
//
//        switch (pbftObj.pRecvBuf.get()[0]) {
//            case CPbft::pubKeyReqHeader:
//                // received msg is pubKeyReq. send pubKey
//                std::cout << "receive pubKey req" << std::endl;
//                // send public key to the peer.
//                pbftObj.sendPubKey(src_addr, deserializePublicKeyReq(pbftObj.pRecvBuf.get(), recvBytes));
//                continue;
//            case CPbft::pubKeyMsgHeader:
//                deSerializePubKeyMsg(pbftObj.peers, pbftObj.pRecvBuf.get(), recvBytes, src_addr);
//                continue;
//            case CPbft::clientReqHeader:
//                // received client request, send preprepare
//#ifdef BASIC_PBFT
//                std::cout << "receive client req" << std::endl;
//#endif
//                uint32_t seq = pbftObj.nextSeq++;
//                std::string clientReq(&pbftObj.pRecvBuf.get()[2], recvBytes - 2);
//                CPre_prepare pp = pbftObj.assemblePre_prepare(seq, clientReq);
//                pbftObj.broadcast(&pp);
//                continue;
//        }
//
//
//        // received msg is a PbftMessage.	
//        std::string recvString(pbftObj.pRecvBuf.get(), recvBytes);
//        std::istringstream iss(recvString);
//        int phaseNum = -1;
//        iss >> phaseNum;
//        switch (static_cast<PbftPhase> (phaseNum)) {
//            case pre_prepare:
//            {
//                CPre_prepare ppMsg(pbftObj.server_id);
//#ifdef SOCKET
//                std::cout << "recvBytes = " << recvBytes << std::endl;
//#endif
//                ppMsg.Unserialize(iss);
//                pbftObj.ProcessPP(ppMsg);
//                break;
//            }
//            case prepare:
//            {
//                CPbftMessage pMsg(PbftPhase::prepare, pbftObj.server_id);
//                pMsg.deserialize(iss);
//                pbftObj.ProcessP(pMsg, true);
//                break;
//            }
//            case commit:
//            {
//                CPbftMessage cMsg(PbftPhase::commit, pbftObj.server_id);
//                cMsg.deserialize(iss);
//                pbftObj.ProcessC(cMsg, true);
//                break;
//            }
//            default:
//                std::cout << "received invalid msg" << std::endl;
//
//        }
//    }
//}

//void CPbft::start() {
//    receiver = std::thread(interruptableReceive, std::ref(*this));
//    receiver.join();
//}

bool CPbft::ProcessPP(CNode* pfrom, CPre_prepare& ppMsg) {
//#ifdef BASIC_PBFT
//    std::cout << "server " << server_id << "received pre-prepare, seq = " << pre_prepare.seq << std::endl;
//#endif
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
//    CPbftMessage p = assembleMsg(PbftPhase::prepare, ppMsg.seq);
//    broadcast(&p);
//    if (log[ppMsg.seq].prepareCount >= (nFaulty << 1)) {
//        log[ppMsg.seq].phase = PbftPhase::commit;
//        CPbftMessage c = assembleMsg(PbftPhase::commit, ppMsg.seq);
//        broadcast(&c);
//    } else {
//#ifdef BASIC_PBFT
//        std::cout << "enter prepare phase. seq in pre-prepare = " << ppMsg.seq << std::endl;
//#endif
//        log[ppMsg.seq].phase = PbftPhase::prepare;
//    }
    std::cout << "digest = " << ppMsg.digest.GetHex() << std::endl;
    return true;
}

bool CPbft::ProcessP(CPbftMessage& prepare, bool sanityCheck) {
//#ifdef BASIC_PBFT
//    std::cout << "server " << server_id << " received prepare. seq = " << prepare.seq << std::endl;
//#endif
//    // sanity check for signature, seq, view.
//    if (sanityCheck && !checkMsg(&prepare)) {
//        return false;
//    }

//    // if phase is prepare or commit, also need to check view 
//    if (msg->phase == PbftPhase::prepare || msg->phase == PbftPhase::commit) {
//
//        if (log[msg->seq].pre_prepare.view != msg->view) {
//            std::cerr << "log entry view = " << log[msg->seq].pre_prepare.view << ", but msg view = " << msg->view << std::endl;
//            return false;
//        }
//    }
//
//    //-----------add to log (currently use placeholder: should add the entire message to log and not increase re-count if the sender is the same. Also, if prepares are received earlier than pre-prepare, different prepare may have different digest. Should categorize buffered prepares based on digest.)
//    //    log[prepare.seq].prepareArray.push_back(prepare);
//    // count the number of prepare msg. enter commit if greater than 2f
//    log[prepare.seq].prepareCount++;
//    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
//    if (log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareCount == (nFaulty << 1)) {
//        // enter commit phase
//#ifdef BASIC_PBFT
//        std::cout << "server " << server_id << " enter commit phase" << std::endl;
//#endif
//        log[prepare.seq].phase = PbftPhase::commit;
//        CPbftMessage c = assembleMsg(PbftPhase::commit, prepare.seq);
//        broadcast(&c);
//        return true;
//    }
    return true;
}

bool CPbft::ProcessC(CPbftMessage& commit, bool sanityCheck) {
//#ifdef BASIC_PBFT
//    std::cout << "server " << server_id << "received commit, seq = " << commit.seq << std::endl;
//#endif
//    // sanity check for signature, seq, view.
//    if (sanityCheck && !checkMsg(&commit)) {
//        return false;
//    }

//    // if phase is prepare or commit, also need to check view 
//    if (msg->phase == PbftPhase::prepare || msg->phase == PbftPhase::commit) {
//
//        if (log[msg->seq].pre_prepare.view != msg->view) {
//            std::cerr << "log entry view = " << log[msg->seq].pre_prepare.view << ", but msg view = " << msg->view << std::endl;
//            return false;
//        }
//    }
//
//    // count the number of prepare msg. enter execute if greater than 2f+1
//    log[commit.seq].commitCount++;
//    if (log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitCount == (nFaulty << 1) + 1) {
//        // enter execute phase
//#ifdef BASIC_PBFT
//        std::cout << "enter reply phase" << std::endl;
//#endif
//        log[commit.seq].phase = PbftPhase::reply;
//        executeTransaction(commit.seq);
//        return true;
//    }
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
    uint32_t seq = nextSeq++;
    CPre_prepare toSent; // phase is set to Pre_prepare by default.
    toSent.seq = seq;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = tx.GetHash();
    toSent.tx = CMutableTransaction(tx);
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so clientReq is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

// TODO: the real param should include digest, i.e. the block header hash.----(currently use placeholder)

//CPbftMessage CPbft::assembleMsg(PbftPhase phase, uint32_t seq) {
//    CPbftMessage toSent(log[seq].pre_prepare, server_id);
//    toSent.phase = phase;
//    uint256 hash;
//    toSent.getHash(hash);
//    privateKey.Sign(hash, toSent.vchSig);
//    return toSent;
//}

//CReply CPbft::assembleReply(uint32_t seq) {
//    /* we use the digest from globalCC rather than digest from pre-prepare because 
//     * a group may get a globalCC without GC msg from its own group.
//     */
//    std::string req = log[seq].pre_prepare.clientReq;
//    // the last field of a requst is the timestamp
//    CReply toSent(seq, server_id, log[seq].result, log[seq].pre_prepare.digest, req.substr(req.find_last_of(',') + 1));
//    uint256 hash;
//    toSent.getHash(hash);
//    privateKey.Sign(hash, toSent.vchSig);
//    return toSent;
//}
//
//void CPbft::broadcast(CPbftMessage* msg) {
//    std::ostringstream oss;
//    if (msg->phase == PbftPhase::pre_prepare) {
//        (static_cast<CPre_prepare*> (msg))->Serialize(oss);
//    } else {
//        msg->serialize(oss);
//    }
//    // loop to  send prepare to all nodes in the peers map.
//    for (auto p : peers) {
//        udpClient.sendto(oss, p.second.ip, p.second.port);
//    }
//    // virtually send the message to this node itself if it is a prepare or commit msg.
//    switch (msg->phase) {
//        case PbftPhase::pre_prepare:
//            // do call onReceivePrePrepare, because the leader do not send prepare.
//            if (log[msg->seq].pre_prepare.digest.IsNull()) {
//                // add to log, phase is  auto-set to prepare
//                log[msg->seq] = CPbftLogEntry(*(static_cast<CPre_prepare*> (msg)));
//#ifdef BASIC_PBFT
//                std::cout << "add to log, clientReq =" << (static_cast<CPre_prepare*> (msg))->clientReq << std::endl;
//#endif
//            }
//
//            break;
//        case PbftPhase::prepare:
//            onReceivePrepare(const_cast<CPbftMessage&> (*msg), false);
//            break;
//        case PbftPhase::commit:
//            onReceiveCommit(const_cast<CPbftMessage&> (*msg), false);
//            break;
//        default:
//            break;
//    }
//}
//
//void CPbft::executeTransaction(const int seq) {
//    // execute all lower-seq tx until this one if possible.
//    int i = lastExecutedIndex + 1;
//    for (; i < seq + 1; i++) {
//        if (log[i].phase == PbftPhase::reply) {
//            /* client request message format: "r <request type>,<key>[,<value>]". 
//             * Request type can be either read 'r' or write 'w'. If it is a 
//             * write request, client must provide value. We use comma as delimiter 
//             * here to avoid coflict with message deserialization, which use space
//             * as delimiter.
//             */
//            std::string req = log[i].pre_prepare.clientReq;
//
//            if (req.at(0) == 'w') {
//                // this is a write request
//                std::size_t found = req.find(',', 2);
//                int key = std::stoi(req.substr(2, found - 2));
//                data[key] = req.at(found + 1);
//                log[i].result = '0'; // '0' means write succeed. 
//#ifdef EXECUTION
//                std::cout << "-----server " << server_id << " write key: " << key << ", value :"
//                        << data[key] << std::endl;
//#endif
//            } else if (req.at(0) == 'r') {
//                /* Empty string means read failed because all writes come together with 
//                 * a write value and empty string simply means the key has never been 
//                 * inserted into the map.
//                 */
//                int key = std::stoi(req.substr(2));
//                log[i].result = data[key];
//#ifdef EXECUTION
//                std::cout << "-----server " << server_id << " read key: " << key << ", value :"
//                        << data[key] << std::endl;
//#endif
//            } else {
//                std::cout << "invalid request" << std::endl;
//            }
//            /* send reply right after execution. */
//            sendReply2Client(i);
//        } else {
//            break;
//        }
//    }
//    lastExecutedIndex = i - 1;
//    /* if lastExecutedIndex is less than seq, we delay sending reply until 
//     * the all requsts up to seq has been executed. This may be triggered 
//     * by future requests.
//     */
//}
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


CPbftClient::CPbftClient(): nTxTotal(1), nTxSent(0) {}

std::unique_ptr<CPbft> g_pbft;
std::unique_ptr<CPbftClient> g_pbft_client;