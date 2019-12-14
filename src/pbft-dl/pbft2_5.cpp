#include "pbft-dl/debug_flags.h"
#include "pbft-dl/pbft2_5.h"
#include "pbft-dl/pbft-dl.h"
#include "pbft-dl/cert.h"
#include "init.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "pbft/peer.h"
#include "pbft/util.h"

CPbft2_5::CPbft2_5(): nFaulty(1), nFaultyGroups(1), localLeader(0), localView(0), globalView(0), nextSeq(0), lastExecutedIndex(-1), server_id(INT_MAX) {}

// pRecvBuf must be set large enough to receive cross group msg.
CPbft2_5::CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader, uint32_t numFaultyGroups): nFaulty(1), nFaultyGroups(numFaultyGroups), localLeader(l_leader), localView(0), globalView(0), log(std::vector<DL_LogEntry>(CPbft::logSize, DL_LogEntry(nFaulty))), nextSeq(0), lastExecutedIndex(-1), server_id(id), udpServer(new UdpServer("localhost", serverPort)), udpClient(UdpClient()), pRecvBuf(new char[(2 * nFaultyGroups + 1) * (2 * nFaulty + 1) * CIntraGroupMsg::messageSizeBytes], std::default_delete<char[]>()), privateKey(CKey()) {
#ifdef BASIC_PBFT 
    std::cout << "CPbft2_5 constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
#endif
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    CPbftPeer myself("localhost", serverPort, publicKey); 
#ifdef BASIC_PBFT 
    std::cout << "my serverId = " << server_id << ", publicKey = " << publicKey.GetHash().ToString() <<std::endl;
#endif
}    

CPbft2_5& CPbft2_5::operator = (const CPbft2_5& rhs){
    if(this == &rhs)
	return *this;
    nFaulty = rhs.nFaulty; 
    nFaultyGroups = rhs.nFaultyGroups;
    localLeader = rhs.localLeader;
    dlHandler = rhs.dlHandler;
    localView = rhs.localView;
    globalView = rhs.globalView;
    log = rhs.log;
    nextSeq = rhs.nextSeq; 
    lastExecutedIndex = rhs.lastExecutedIndex;
    leader = rhs.leader;
    members = rhs.members;
    server_id = rhs.server_id;
    peers = rhs.peers; 
    udpServer = rhs.udpServer;
    udpClient = rhs.udpClient;
    pRecvBuf = rhs.pRecvBuf;
    privateKey = rhs.privateKey;
    publicKey = rhs.publicKey; 
    data = rhs.data; 
    return *this;
    
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
	
	ssize_t recvBytes =  pbft2_5Obj.udpServer->timed_recv(pbft2_5Obj.pRecvBuf.get(), (2 * pbft2_5Obj.nFaultyGroups + 1)*(2 * pbft2_5Obj.nFaulty + 1) * CIntraGroupMsg::messageSizeBytes, 500, &src_addr, &len);
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}
	
#ifdef BASIC_PBFT 
	std::cout << "recvBytes = " << recvBytes << std::endl;
#endif
	switch(pbft2_5Obj.pRecvBuf.get()[0]){
	    case CPbft::pubKeyReqHeader:
		// received msg is pubKeyReq. send pubKey
#ifdef BASIC_PBFT 
		std::cout << "receive pubKey req" << std::endl;
#endif
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
		CCrossGroupMsg gppMsg(DL_Phase::DL_GPP);
		gppMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGPP(gppMsg);
		break;
	    }
	    case static_cast<int>(DL_GP):
	    {
		CCrossGroupMsg gpMsg(DL_Phase::DL_GP);
		gpMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGP(gpMsg, true);
		break;
	    }
	    case DL_GPCD:
	    {
		CCertMsg gpcdMsg(DL_Phase::DL_GPCD, 1* pbft2_5Obj.nFaultyGroups * 2 + 1);
		gpcdMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGPCD(gpcdMsg);
		break;
	    }
	    case DL_GPLC:
	    {
		CIntraGroupMsg gplcMsg(DL_Phase::DL_GPLC, pbft2_5Obj.server_id);
		gplcMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGPLC(gplcMsg);
		break;
	    }
	    case DL_GC:
	    {
		CCrossGroupMsg gcMsg(DL_Phase::DL_GC);
		gcMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGC(gcMsg, true);
		break;
	    }
	    case DL_GCCD:
	    {
		CCertMsg gccdMsg(DL_Phase::DL_GCCD, pbft2_5Obj.server_id);
		gccdMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGCCD(gccdMsg);
		break;
	    }
	    case DL_LR:
	    {
		CLocalReply lrMsg;
		lrMsg.deserialize(iss);
		pbft2_5Obj.onReceiveLR(lrMsg);
		break;
	    }
	    default:
		std::cout << "server " << pbft2_5Obj.server_id << " received invalid msg" << std::endl;
	}
    }
}

void CPbft2_5::start(){
    receiver = std::thread(DL_Receive, std::ref(*this)); 
    receiver.join();
}

bool CPbft2_5::onReceivePrePrepare(CLocalPP& pre_prepare){
#ifdef INTRA_GROUP_DEBUG
    std::cout<< "server " << server_id << " received pre-prepare" << std::endl;
#endif
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
#ifdef INTRA_GROUP_DEBUG
	std::cout << "server " << server_id << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
#endif
	log[pre_prepare.seq].phase = DL_prepare;
    }
    return true;
}

bool CPbft2_5::onReceivePrepare(CIntraGroupMsg& prepare, bool sanityCheck){
#ifdef INTRA_GROUP_DEBUG
    std::cout << "2_5 received prepare." << std::endl;
#endif
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&prepare)){
	return false;
    }
    
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
    if(log[prepare.seq].phase ==DL_prepare && log[prepare.seq].prepareCount == (nFaulty << 1)){
	// enter commit phase
#ifdef INTRA_GROUP_DEBUG
	std::cout << "server " << server_id << " enter commit phase" << std::endl;
#endif
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
#ifdef INTRA_GROUP_DEBUG
    std::cout << "server " << server_id << "received commit" << std::endl;
#endif
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&commit)){
	return false;
    }
    
    // count the number of commit msg. 
    log[commit.seq].localCC.push_back(commit);
    if(log[commit.seq].phase == DL_commit && log[commit.seq].localCC.size() == (nFaulty << 1 ) + 1 ){ 
#ifdef CROSS_GROUP_DEBUG
	std::cout << "global leader = " << dlHandler.globalLeader << std::endl;
#endif
	if(server_id == dlHandler.globalLeader){
	    // if this node is the global leader, send GPP to other group leaders.
#ifdef CROSS_GROUP_DEBUG
	    std::cout << "server " << server_id << " multicast GPP " << log[commit.seq].pre_prepare.clientReq << std::endl;
#endif
	    CCrossGroupMsg gpp = assembleGPP(commit.seq);
	    log[commit.seq].globalPC.push_back(gpp);
	    dlHandler.sendGlobalMsg2Leaders(gpp, udpClient);
	} else {
	    //this node is a local leader, send GP to other group leaders.
#ifdef CROSS_GROUP_DEBUG
	    std::cout << "server " << server_id << " multicast GP " << log[commit.seq].pre_prepare.clientReq << std::endl;
#endif
	    CCrossGroupMsg gp = assembleGP(commit.seq);
	    dlHandler.sendGlobalMsg2Leaders(gp, udpClient, this);
	}
	return true;
    }
    return true;
}

bool CPbft2_5::onReceiveGPP(CCrossGroupMsg& gpp){
#ifdef CROSS_GROUP_DEBUG
    std::cout << "server " << server_id << " receieved GPP, seq = " << gpp.localCC[0].seq << std::endl;
#endif
    if(!dlHandler.checkGPP(gpp, globalView, log))
	return false;
    // add to globalPC
    log[gpp.localCC[0].seq].globalPC.push_back(gpp);
    CLocalPP pp = assemblePre_prepare(gpp.localCC[0].seq, gpp.clientReq);
    // TODO: should have GPP be broadcast together, and group members need also check if GPP is valid.
    // must create a log entry before broadcasting pp msg b/c broadcast is gonna create new log entry if pre_prepare.digest is null.
    log[gpp.localCC[0].seq].pre_prepare = pp;
    log[gpp.localCC[0].seq].phase = DL_prepare;
    broadcast(&pp);
    return true;
}

bool CPbft2_5::onReceiveGP(CCrossGroupMsg& gp, bool sanityCheck){
    // TODO: how to tolerate network reodering issue? GP may arrive before GPP and a node do not know the correct req for a seq before it receives a GPP.
#ifdef CROSS_GROUP_DEBUG
    std::cout << "server " << server_id << " receieved GP, digest = " << gp.localCC[0].digest.GetHex() << std::endl;
#endif
    // TODO: must check if the digest matches req in GPP.
    if(sanityCheck && !dlHandler.checkGP(gp, globalView, log))
	return false;
    // add to globalPC
    log[gp.localCC[0].seq].globalPC.push_back(gp);
    // if the globalPC reaches the size of 2F+1, send it to groupmates.
#ifdef CROSS_GROUP_DEBUG
    std::cout << "server " << server_id << " seq = " << gp.localCC[0].seq <<  " GlobalPC size = " << log[gp.localCC[0].seq].globalPC.size() << std::endl;
#endif
    // TODO: what if there is no gpp in globalPC?
    if(log[gp.localCC[0].seq].globalPC.size() == (nFaultyGroups << 1) + 1){
	// send a gpcd message to local followers
	CCertMsg cert(DL_GPCD, 2 * nFaultyGroups + 1, log[gp.localCC[0].seq].globalPC);
	dlHandler.multicastCert(cert, udpClient, peers);
	/* collected enough gp messages, it is time to enter global_PC_local_commit phase. */
	log[gp.localCC[0].seq].phase = DL_GPLC;
	// the local leader adds a GPLC msg to itself log
	CIntraGroupMsg c = assembleMsg(DL_GPLC, gp.localCC[0].seq); 
	log[gp.localCC[0].seq].GPLC.push_back(c);
    }
    return true;
}

bool CPbft2_5::onReceiveGPCD(const CCertMsg& gpcd){
    if(!dlHandler.checkGPCD(gpcd, globalView, log))
	return false;
    // add to globalPC
    log[gpcd.globalCert[0].localCC[0].seq].globalPC = gpcd.globalCert;
    // send commit to local leader to ack receiving globalPC
    CIntraGroupMsg c = assembleMsg(DL_GPLC, gpcd.globalCert[0].localCC[0].seq); 
    // send commit only to local leader
    send2Peer(localLeader, &c);
    return true;
}

bool CPbft2_5::onReceiveGPLC(CIntraGroupMsg& gplc) {
#ifdef INTRA_GROUP_DEBUG
    std::cout << "local leader = " << server_id << "received gplc from follwer " << gplc.senderId << std::endl;
#endif
    // sanity check for signature, seq, view.
    if(!checkMsg(&gplc)){
	return false;
    }
    
    // count the number of gplc msg. 
    log[gplc.seq].GPLC.push_back(gplc);
    if(log[gplc.seq].phase == DL_GPLC && log[gplc.seq].GPLC.size() == (nFaulty << 1 ) + 1 ){ 
#ifdef INTRA_GROUP_DEBUG
	std::cout << "local leader = " << server_id << " enters Global commit phase by sending DL_GC message." << std::endl;
#endif
        CCrossGroupMsg gc = assembleGC(gplc.seq);
        dlHandler.sendGlobalMsg2Leaders(gc, udpClient, this);
	return true;
    }
    return true;
}


bool CPbft2_5::onReceiveGC(CCrossGroupMsg& gc, bool sanityCheck) {
#ifdef CROSS_GROUP_DEBUG
    std::cout << "server " << server_id << " receieved GC, digest = " << gc.localCC[0].digest.GetHex() << std::endl;
#endif
    // TODO: must check if the digest matches req in GPP.
    if(!dlHandler.checkGC(gc, globalView, log))
	return false;
    // add to globalCC
    log[gc.localCC[0].seq].globalCC.push_back(gc);
    // if the globalCC reaches the size of 2F+1, send it to groupmates.
#ifdef CROSS_GROUP_DEBUG
    std::cout << "server " << server_id << " seq = " << gc.localCC[0].seq <<  " -------------GlobalCC size = " << log[gc.localCC[0].seq].globalCC.size() << std::endl;
#endif
    /* we don't check the phase here bacause as long as we collect enough GC messages,
     * we do not need the GC message from our own group.
     */ 
    if(log[gc.localCC[0].seq].globalCC.size() == (nFaultyGroups << 1) + 1){
	// send a gpcd message to local followers
	CCertMsg cert(DL_GCCD, 2 * nFaultyGroups + 1, log[gc.localCC[0].seq].globalCC);
	dlHandler.multicastCert(cert, udpClient, peers);
	/* the local leader execute the request if all proceeding requests have been executed.*/
	// enter local reply phase
	log[gc.localCC[0].seq].phase = DL_Phase::DL_LR;
	executeTransaction(gc.localCC[0].seq);
	
    }
    return true;
}

bool CPbft2_5::onReceiveGCCD(const CCertMsg& gccd){
    if(!dlHandler.checkGCCD(gccd, globalView, log))
	return false;
    // add to globalCC
    log[gccd.globalCert[0].localCC[0].seq].globalCC = gccd.globalCert;
    // enter local reply phase
    log[gccd.globalCert[0].localCC[0].seq].phase = DL_Phase::DL_LR;
    // executeTransaction
    executeTransaction(gccd.globalCert[0].localCC[0].seq);
    return true;
}

bool CPbft2_5::onReceiveLR(CLocalReply& lr) {
#ifdef INTRA_GROUP_DEBUG
    std::cout << "local leader = " << server_id << "received local reply from follwer " << lr.senderId << std::endl;
#endif
    // sanity check for signature, seq, view.
    if(!checkMsg(lr)){
	return false;
    }
    
    // count the number of matching reply msg. 
    log[lr.seq].localReplies.push_back(lr);
    if(log[lr.seq].localReplies.size() == (nFaulty << 1 ) + 1 ){ 
#ifdef INTRA_GROUP_DEBUG
	std::cout << "local leader = " << server_id << " get enough local replies" << std::endl;
	std::cout << "local leader = " << server_id << " get 2f+1 local replies" << std::endl;
#endif
	// create a global reply message
        CGlobalReply globalReply = assembleGR(lr.seq);
	// send global reply msg directly to client
        dlHandler.sendGlobalReply(globalReply, udpClient);
	return true;
    }
    return true;
}

void CPbft2_5::executeTransaction(const int seq){
    /* TODO: 
     * ? 3. a local leader should keep a hashmap mapping results to a set of reply msg.
     * Whenever a set grows to f + 1, send the whole set to local leader.
     * 4. pre-prepare msg should have client ip and udp port so that local leaders can
     * send global reply msg back to client.
     */
    
    // execute all lower-seq tx until this one if possible.
    int i = lastExecutedIndex + 1;
    for(; i < seq + 1; i++){
	if(log[i].phase == DL_Phase::DL_LR){
	    /* client request message format: "r <request type>,<key>[,<value>]". Request type 
	     * can be either read 'r' or write 'w'. If it is a write request, client must provide
	     * value. We use comma as delimiter here to avoid coflict with message deserialization,
	     * which use space as delimiter.
	     */
	    std::string req = log[i].pre_prepare.clientReq; 
	    
	    if(req.at(0) == 'w'){
		// this is a write request
		std::size_t found = req.find(',', 2);
		int key = std::stoi(req.substr(2, found - 2));
		data[key] = req.at(found + 1);
		log[i].result = '0';  // '0' means write succeed. 
#ifdef EXECUTION
		std::cout << "-----server " << server_id << " write key: " << key << ", value :" 
			<< data[key] << std::endl;
#endif
	    } else if(req.at(0) == 'r') {
		/* Empty string means read failed because all writes come together with 
		 * a write value and empty string simply means the key has never been 
		 * inserted into the map.
		 */
		int key = std::stoi(req.substr(2));
		log[i].result = data[key];  
#ifdef EXECUTION
		std::cout << "-----server " << server_id << " read key: " << key << ", value :" 
			<< data[key] << std::endl;
#endif
	    } else {
		std::cout << "invalid request" << std::endl;
	    }
	    /* send reply right after execution. */
	    // send local reply to local leader 
	    CLocalReply r = assembleLocalReply(i); 
	    if (server_id != localLeader){
		send2Peer(localLeader, r);
	    } else {
		// add the local execution result to replies array
		CLocalReply r = assembleLocalReply(i); 
		log[i].localReplies.push_back(r);
	    }
	} else {
	    break;
	}
    }
    lastExecutedIndex = i-1;
    /* if lastExecutedIndex is less than seq, we delay sending reply until 
     * the all requsts up to seq has been executed. This may be triggered 
     * by future requests.
     */
}


CCrossGroupMsg CPbft2_5::assembleGPP(uint32_t seq){
#ifdef MSG_ASSEMBLE
    std::cout << "assembe GPP for seq = " << seq << std::endl;
#endif
    return CCrossGroupMsg(DL_GPP, log[seq].localCC, log[seq].pre_prepare.clientReq);
}

CCrossGroupMsg CPbft2_5::assembleGP(uint32_t seq){
    return CCrossGroupMsg(DL_GP, log[seq].localCC);
}

CCrossGroupMsg CPbft2_5::assembleGC(uint32_t seq){
    return CCrossGroupMsg(DL_GC, log[seq].GPLC);
}

CGlobalReply CPbft2_5::assembleGR(uint32_t seq){
    return CGlobalReply(log[seq].localReplies);
}

void CPbft2_5::send2Peer(uint32_t peerId, CIntraGroupMsg* msg){
#ifdef INTRA_GROUP_DEBUG
    std::cout << "server " << server_id << " send " << msg->phase << " msg to server" << peerId << std::endl;
#endif
    std::ostringstream oss;
    msg->serialize(oss); 
    udpClient.sendto(oss, peers.at(peerId).ip, peers.at(peerId).port);
}

void CPbft2_5::send2Peer(uint32_t peerId, CLocalReply& msg) {
#ifdef INTRA_GROUP_DEBUG
    std::cout << "server " << server_id << " send " << msg.phase << " msg to server" << peerId << std::endl;
#endif
    std::ostringstream oss;
    msg.serialize(oss); 
    udpClient.sendto(oss, peers.at(peerId).ip, peers.at(peerId).port);
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
    
    // if phase is prepare or commit, also need to check view and global view.
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
#ifdef INTRA_GROUP_DEBUG
    std::cout << "sanity check succeed" << std::endl;
#endif
    return true;
}


bool CPbft2_5::checkMsg(CLocalReply& msg){
    // verify signature and return wrong if sig is wrong
    if(peers.find(msg.senderId) == peers.end()){
	std::cerr<< "no pub key for the sender" << std::endl;
	return false;
    }
    uint256 msgHash;
    msg.getHash(msgHash);
    if(!peers[msg.senderId].pk.Verify(msgHash, msg.vchSig)){
	std::cerr<< "verification sig fail" << std::endl;
	return false;
    } 
    
    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if(!log[msg.seq].pre_prepare.digest.IsNull() && log[msg.seq].pre_prepare.digest != msg.digest){
	std::cerr<< "digest error. digest in log = " << log[msg.seq].pre_prepare.digest.GetHex() << ", but msg.digest = " << msg.digest.GetHex() << std::endl;
	return false;
    }
    
#ifdef INTRA_GROUP_DEBUG
    std::cout << "local reply sanity check succeed" << std::endl;
#endif
    return true;
}


CLocalPP CPbft2_5::assemblePre_prepare(uint32_t seq, std::string clientReq){
#ifdef INTRA_GROUP_DEBUG
    std::cout << "assembling pre_prepare, client req = " << clientReq << std::endl;
#endif
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

CLocalReply CPbft2_5::assembleLocalReply(uint32_t seq){
    /* we use the digest from globalCC rather than digest from pre-prepare because 
     * a group may get a globalCC without GC msg from its own group.
     */
    std::string req = log[seq].pre_prepare.clientReq;
    CLocalReply toSent(seq, server_id, log[seq].result, log[seq].globalCC[0].localCC[0].digest, req.substr(req.find_last_of(',') + 1));
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
#ifdef MSG_ASSEMBLE 
	std::cout << "server " << server_id <<" sends " << msg->phase << " msg to peer " << p.first << ", ip:port = " << p.second.ip << ":" << p.second.port << std::endl;
#endif
	udpClient.sendto(oss, p.second.ip, p.second.port);
    }
    // virtually send the message to this node itself if it is a prepare or commit msg.
    switch(msg->phase){
	case DL_pre_prepare:
	    // do not call onReceivePrePrepare, because the leader do not send prepare.
	    if(log[msg->seq].pre_prepare.digest.IsNull()){
		// add to log, phase is  auto-set to prepare
		log[msg->seq] = DL_LogEntry(*(static_cast<CLocalPP*>(msg)), nFaulty);
#ifdef INTRA_GROUP_DEBUG
		std::cout<< "add to log, clientReq =" << (static_cast<CLocalPP*>(msg))->clientReq << std::endl;
#endif
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
    serializePubKeyMsg(oss, server_id, udpServer->get_port(), publicKey);
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}


void CPbft2_5::sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id){
    std::ostringstream oss;
    serializePubKeyMsg(oss, server_id, udpServer->get_port(), publicKey);
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