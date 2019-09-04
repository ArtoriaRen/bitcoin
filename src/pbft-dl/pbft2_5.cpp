#include "pbft-dl/pbft2_5.h"
#include "pbft-dl/pbft-dl.h"
#include "init.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "pbft/peer.h"
#include "pbft/util.h"

// pRecvBuf must be set large enough to receive cross group msg.
CPbft2_5::CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader): nFaulty(1), nGroups(1), localLeader(l_leader), localView(0), globalView(0), log(std::vector<DL_LogEntry>(CPbft::logSize, DL_LogEntry(nFaulty))), nextSeq(0), lastExecutedIndex(-1), server_id(id), udpServer(UdpServer("localhost", serverPort)), udpClient(UdpClient()), pRecvBuf(new char[(2 * nFaulty + 1) * CIntraGroupMsg::messageSizeBytes], std::default_delete<char[]>()), privateKey(CKey()), x(-1){
    std::cout << "CPbft2_5 constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    CPbftPeer myself("localhost", serverPort, publicKey); 
    //    peers.insert(std::make_pair(server_id, myself));
    std::cout << "my serverId = " << server_id << ", publicKey = " << publicKey.GetHash().ToString() <<std::endl;
}    

CPbft2_5::~CPbft2_5(){
    
}

void DL_Receive(CPbft2_5& pbft2_5Obj){
    // Placeholder: broadcast myself pubkey, and request others' pubkey.
    pbft2_5Obj.broadcastPubKey();
    pbft2_5Obj.broadcastPubKeyReq(); // request peer publickey
    struct sockaddr_in src_addr; // use this stuct to get sender IP and port
    size_t len = sizeof(src_addr);
    
    while(!ShutdownRequested()){
	/* timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	 * Max recv bytes is set to 2(f+1) * intra_group_message so that a pbft2_5Obj can
	 * receive cross_group_msg.
	 */

	ssize_t recvBytes =  pbft2_5Obj.udpServer.timed_recv(pbft2_5Obj.pRecvBuf.get(), (2 * pbft2_5Obj.nFaulty + 1) * CIntraGroupMsg::messageSizeBytes, 500, &src_addr, &len);
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}

	std::cout << "recvBytes = " << recvBytes << std::endl;
	switch(pbft2_5Obj.pRecvBuf.get()[0]){
	    case CPbft::pubKeyReqHeader:
		// received msg is pubKeyReq. send pubKey
		std::cout << "receive pubKey req" << std::endl;
		// send public key to the peer.
		pbft2_5Obj.sendPubKey(src_addr, deserializePublicKeyReq(pbft2_5Obj.pRecvBuf.get(), recvBytes));
		continue;
	    case CPbft::pubKeyMsgHeader:
		deSerializePubKeyMsg(pbft2_5Obj.peers, pbft2_5Obj.pRecvBuf.get(), recvBytes, src_addr);
		continue;
	    case CPbft::clientReqHeader:
		// received client request, send preprepare.ONLY leader group do this.
		uint32_t seq = pbft2_5Obj.nextSeq++; 
		std::string clientReq(&pbft2_5Obj.pRecvBuf.get()[2], recvBytes - 2);
		CLocalPP pp = pbft2_5Obj.assemblePre_prepare(seq, clientReq);
		pbft2_5Obj.broadcast(&pp);
		continue;
	}
	
	
	// received msg is a PbftMessage or DL-PBFT msg.	
	std::string recvString(pbft2_5Obj.pRecvBuf.get(), recvBytes);
	std::istringstream iss(recvString);
	int phaseNum = -1;
	iss >> phaseNum;
	switch(phaseNum){
	    case static_cast<int>(pre_prepare):
	    {
		CLocalPP ppMsg(pbft2_5Obj.server_id);
		ppMsg.deserialize(iss);
		pbft2_5Obj.onReceivePrePrepare(ppMsg);
		break;
	    }
	    case static_cast<int>(prepare):
	    {
		CIntraGroupMsg pMsg(DL_prepare, pbft2_5Obj.server_id);
		pMsg.deserialize(iss);
		pbft2_5Obj.onReceivePrepare(pMsg, true);
		break;
	    } 
	    case static_cast<int>(commit):
	    {
		CIntraGroupMsg cMsg(DL_commit, pbft2_5Obj.server_id);
		cMsg.deserialize(iss);
		pbft2_5Obj.onReceiveCommit(cMsg, true);
		break;
	    }
	    case static_cast<int>(DL_GPP):
	    {
		std::cout << "server " << pbft2_5Obj.server_id << "received GPP" << std::endl;
		CCrossGroupMsg gppMsg(DL_Phase::DL_GPP);
		gppMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGPP(gppMsg);
	    }
//	    case static_cast<int>(DLP_GP):
//	    {
//		CCrossGroupMsg gpMsg(DL_Phase::DLP_GP, pbft2_5Obj.server_id);
//		gpMsg.deserialize(iss);
//		pbft2_5Obj.onReceiveGP(gpMsg);
//	    }
	    //	    case DLP_GPCD:
	    //	    {
	    //		CCrossGroupMsg gpcdMsg(DL_Phase::DLP_GPCD, pbft2_5Obj.server_id);
	    //		gpcdMsg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGPCD(gpMsg);
	    //	    }
	    //	    case DLC_GPLC:
	    //	    {
	    //		CCrossGroupMsg gpcdMsg(DL_Phase::DLC_GPLC, pbft2_5Obj.server_id);
	    //		gpcdMsg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGPLC(gpMsg);
	    //	    }
	    //	    case DLC_GC:
	    //	    {
	    //		CCrossGroupMsg msg(DL_Phase::DLC_GC, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGC(msg);
	    //	    }
	    //	    case DLC_GCCD:
	    //	    {
	    //		CCrossGroupMsg msg(DL_Phase::DLC_GCCD, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGCCD(msg);
	    //	    }
	    //	    case DLR_LR:
	    //	    {
	    //		CCrossGroupMsg msg(DL_Phase::DLR_LR, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveLR(msg);
	    //	    }
	    //	    case DLR_GR:
	    //	    {
	    //		CCrossGroupMsg msg(DL_Phase::DLR_GR, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGR(msg);
	    //	    }
	    //	    case execute:
	    //		// only the local leader need to handle the execute message?
	    //		std::cout << "received execute msg" << std::endl;
	    //		break;
	    default:
		std::cout << "received invalid msg" << std::endl;
	}
    }
}

void CPbft2_5::start(){
    receiver = std::thread(DL_Receive, std::ref(*this)); 
    receiver.join();
}

bool CPbft2_5::onReceivePrePrepare(CLocalPP& pre_prepare){
    std::cout<< "server " << server_id << " received pre-prepare" << std::endl;
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if(!checkMsg(&pre_prepare)){
	return false;
    }
    // add to log
    log[pre_prepare.seq].pre_prepare = pre_prepare;
    CIntraGroupMsg p = assembleMsg(DL_prepare, pre_prepare.seq); 
    broadcast(&p); 
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */
    if(log[pre_prepare.seq].prepareCount >= (nFaulty << 1)){
	log[pre_prepare.seq].phase = DL_commit;
	CIntraGroupMsg c = assembleMsg(DL_commit, pre_prepare.seq);
	broadcast(&c);
    } else {
	std::cout << "server " << server_id << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
	log[pre_prepare.seq].phase = DL_prepare;
    }
    return true;
}

bool CPbft2_5::onReceivePrepare(CIntraGroupMsg& prepare, bool sanityCheck){
    std::cout << "2_5 received prepare." << std::endl;
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&prepare)){
	return false;
    }
    
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
    if(log[prepare.seq].phase ==DL_prepare && log[prepare.seq].prepareCount == (nFaulty << 1)){
	// enter commit phase
	std::cout << "server " << server_id << " enter commit phase" << std::endl;
	log[prepare.seq].phase = DL_commit;
	CIntraGroupMsg c = assembleMsg(DL_commit, prepare.seq); 
	if(server_id != localLeader){
	    // send commit only to local leader
	    send2Peer(localLeader, &c);
	} else {
	    log[prepare.seq].localCC.push_back(c);
	}
	
	return true;
    }
    return true;
}

bool CPbft2_5::onReceiveCommit(CIntraGroupMsg& commit, bool sanityCheck){
    std::cout << "received commit" << std::endl;
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&commit)){
	return false;
    }
    
    // count the number of prepare msg. 
    log[commit.seq].localCC.push_back(commit);
    if(log[commit.seq].phase == DL_commit && log[commit.seq].localCC.size() == (nFaulty << 1 ) + 1 ){ 
	std::cout << "global leader = " << dlHandler.globalLeader << std::endl;
	if(server_id == dlHandler.globalLeader){
	    // if this node is the global leader, send GPP to other group leaders.
	    std::cout << "server " << server_id << " multicast GPP " << log[commit.seq].pre_prepare.clientReq << std::endl;
	    CCrossGroupMsg gpp = assembleGPP(commit.seq);
	    log[commit.seq].globalPC.push_back(gpp);
	    dlHandler.sendGPP2Leaders(gpp, udpClient);
	} else {
	    //this node is a local leader, send GP to other group leaders.
	    std::cout << "server " << server_id << " multicast GP " << log[commit.seq].pre_prepare.clientReq << std::endl;
	    CCrossGroupMsg gp = assembleGP(commit.seq);
	    dlHandler.sendMsg2Leaders(gp);
	}
	return true;
    }
    return true;
}

bool CPbft2_5::onReceiveGPP(CCrossGroupMsg& gpp){
    std::cout << "server " << server_id << " receieved GPP, req = " << gpp.clientReq << std::endl;
    // TODO: check all commits in the localCC
    return true;
}

bool CPbft2_5::onReceiveGP(CCrossGroupMsg& commit){
    return true;
}

/* This function does not execute tx. Instead, it send a GP or 
 */
void CPbft2_5::executeTransaction(const int seq){
//    std::ostringstream oss;
//    // serialize all commit messages into one stream.
//    for(auto c : commitList.at(seq)){
//	c.serialize(oss); 
//    }
//    
//    // send the stream to all group leader
//    for(auto it = dlHandler.peerGroupLeaders.begin(); it != dlHandler.peerGroupLeaders.end(); it++){
//	udpClient.sendto(oss, it->second.ip, it->second.port);
//	
//    }
    
}


CCrossGroupMsg CPbft2_5::assembleGPP(uint32_t seq){
    return CCrossGroupMsg(DL_GPP, log[seq].localCC, log[seq].pre_prepare.clientReq);
}

CCrossGroupMsg CPbft2_5::assembleGP(uint32_t seq){
    return CCrossGroupMsg();
}


void CPbft2_5::send2Peer(uint32_t peerId, CIntraGroupMsg* msg){
    std::ostringstream oss;
    msg->serialize(oss); 
    udpClient.sendto(oss, peers[peerId].ip, peers[peerId].port);
}

bool CPbft2_5::checkMsg(CIntraGroupMsg* msg){
    // verify signature and return wrong if sig is wrong
    if(peers.find(msg->senderId) == peers.end()){
	std::cerr<< "no pub key for the sender" << std::endl;
	return false;
    }
    uint256 msgHash;
    msg->getHash(msgHash);
    if(!peers[msg->senderId].pk.Verify(msgHash, msg->vchSig)){
	std::cerr<< "verification sig fail" << std::endl;
	return false;
    } 
    // server should be in the local view
    if(localView != msg->localView){
	std::cerr<< "server local view = " << localView << ", but msg local view = " << msg->localView << std::endl;
	return false;
    }

    // server should be in the global view
    if(globalView != msg->globalView){
	std::cerr<< "server global view = " << globalView << ", but msg global view = " << msg->globalView << std::endl;
	return false;
    }
    
    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if(!log[msg->seq].pre_prepare.digest.IsNull() && log[msg->seq].pre_prepare.digest != msg->digest){
	std::cerr<< "digest error. digest in log = " << log[msg->seq].pre_prepare.digest.GetHex() << ", but msg->digest = " << msg->digest.GetHex() << std::endl;
	return false;
    }

    // if phase is pre-prepare, check if the digest matches client req
    if(msg->phase == DL_pre_prepare){
	std::string req = ((CLocalPP*)msg)->clientReq;
	
	if(msg->digest != Hash(req.begin(), req.end())){
	    std::cerr<< "digest does not match client request. Client req = " << req << ", but digest = " << msg->digest.GetHex() << std::endl;
	    return false;
	    
	}
    }
    
    // if phase is prepare or commit, also need to check view  and digest value.
    if(msg->phase == DL_prepare || msg->phase == DL_commit){
	if(log[msg->seq].pre_prepare.localView != msg->localView){
	    std::cerr<< "log entry local view = " << log[msg->seq].pre_prepare.localView << ", but msg local view = " << msg->localView << std::endl;
	    return false;
	}
	if(log[msg->seq].pre_prepare.localView != msg->localView){
	    std::cerr<< "log entry global view = " << log[msg->seq].pre_prepare.globalView << ", but msg global view = " << msg->globalView << std::endl;
	    return false;
	}
    }
    std::cout << "sanity check succeed" << std::endl;
    return true;
}



CLocalPP CPbft2_5::assemblePre_prepare(uint32_t seq, std::string clientReq){
    std::cout << "assembling pre_prepare, client req = " << clientReq << std::endl;
    CLocalPP toSent(server_id); // phase is set to Pre_prepare by default.
    toSent.seq = seq;
    toSent.localView = localView;
    toSent.globalView = globalView;
//    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = Hash(clientReq.begin(), clientReq.end());
    toSent.clientReq = clientReq;
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so clientReq is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

// TODO: the real param should include digest, i.e. the block header hash.----(currently use placeholder)
CIntraGroupMsg CPbft2_5::assembleMsg(DL_Phase phase, uint32_t seq){
    CIntraGroupMsg toSent(log[seq].pre_prepare, server_id);
    toSent.phase = phase;
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

void CPbft2_5::broadcast(CIntraGroupMsg* msg){
    std::ostringstream oss;
    if(msg->phase == DL_pre_prepare){
	(static_cast<CLocalPP*>(msg))->serialize(oss); 
    } else {
	msg->serialize(oss); 
    }
    // loop to  send prepare to all nodes in the peers map.
    for(auto p: peers){
	udpClient.sendto(oss, p.second.ip, p.second.port);
    }
    // virtually send the message to this node itself if it is a prepare or commit msg.
    switch(msg->phase){
	case DL_pre_prepare:
	    // do not call onReceivePrePrepare, because the leader do not send prepare.
	    if(log[msg->seq].pre_prepare.digest.IsNull()){
		// add to log, phase is  auto-set to prepare
		log[msg->seq] = DL_LogEntry(*(static_cast<CLocalPP*>(msg)), nFaulty);
		std::cout<< "add to log, clientReq =" << (static_cast<CLocalPP*>(msg))->clientReq << std::endl;
	    }
	    
	    break;
	case DL_prepare:
	    onReceivePrepare(const_cast<CIntraGroupMsg&>(*msg), false);
	    break;
	case DL_commit:
	    onReceiveCommit(const_cast<CIntraGroupMsg&>(*msg), false);
	    break;
	default:
	    break;
    }
}

CPubKey CPbft2_5::getPublicKey(){
    return publicKey;
}

void CPbft2_5::broadcastPubKey(){
    std::ostringstream oss;
    // opti: serialized version can be stored.
    serializePubKeyMsg(oss, server_id, udpServer.get_port(), publicKey);
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}


void CPbft2_5::sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id){
    std::ostringstream oss;
    serializePubKeyMsg(oss, server_id, udpServer.get_port(), publicKey);
    udpClient.sendto(oss, inet_ntoa(src_addr.sin_addr), peers.at(recver_id).port);
}


void CPbft2_5::broadcastPubKeyReq(){
    std::ostringstream oss;
    oss << CPbft::pubKeyReqHeader;
    oss << " ";
    oss << server_id;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}