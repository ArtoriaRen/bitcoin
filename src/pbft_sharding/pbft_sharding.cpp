/*
 * to change this license header, choose license headers in project properties.
 * to change this template file, choose tools | templates
 * and open the template in the editor.
 */

// TODO: buffer future prepare and commit.



#include <locale>
#include<string.h>
#include <netinet/in.h>
//#include "pbft/pbft.h"
#include "pbft_sharding/pbft_sharding.h"
#include "init.h"
#include "pbft_sharding/msg.h"
#include "pbft_sharding/log_entry.h"
#include "crypto/aes.h"
#include "pbft/peer.h"
#include "debug_flag.h"
#include "primitives/transaction.h"

CPbftSharding::CPbftSharding(): groupSize(4), localView(0), nextSeq(0), lastExecutedIndex(-1), server_id(INT_MAX), nFaulty(1){}

CPbftSharding::CPbftSharding(int serverPort, unsigned int id, size_t numNodes): groupSize(numNodes), localView(0),log(std::vector<CLogEntry>(CPbftSharding::logSize)), nextSeq(0), lastExecutedIndex(-1), members(std::vector<uint32_t>(groupSize)), server_id(id), nGroups(1), udpServer(new UdpServer("localhost", serverPort)), udpClient(UdpClient()), pRecvBuf(new char[Message::messageSizeBytes], std::default_delete<char[]>()), privateKey(CKey()), x(-1){
    nFaulty = (members.size() - 1)/3;
    std::cout << "CPbftSharding constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    CPbftPeer myself("localhost", serverPort, publicKey); 
    //    peers.insert(std::make_pair(server_id, myself));
    std::cout << "my serverId = " << server_id << ", publicKey = " << publicKey.GetHash().ToString() <<std::endl;
}


CPbftSharding::~CPbftSharding(){
    
}

CPbftSharding& CPbftSharding::operator = (const CPbftSharding& rhs){
    if(this == &rhs)
	return *this;
    groupSize = rhs.groupSize;
    nFaulty = rhs.nFaulty; 
    nGroups = rhs.nGroups;
    localView = rhs.localView;
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

/**
 * Go through the last nBlocks block, calculate membership of nGroups groups.
 * @param random is the random number used to group nodes.
 * @param nBlocks is number of blocks whose miner participate in the PBFT.
 * @return 
 */
void CPbftSharding::group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindexNew) {
    const CBlockIndex* pindex = pindexNew; // make a copy so that we do not change the original argument passed in
    LogPrintf("group number %d nBlock = %d, pindex->nHeight = %d \n", nGroups, nBlocks, pindex->nHeight);
    for (uint i = 0; i < nBlocks && pindex != nullptr; i++) {
        //TODO: get block miner IP addr and port, add them to the members
        LogPrintf("pbft: block height = %d, ip&port = %s \n ", pindex->nHeight, pindex->netAddrPort.ToString());
        pindex = pindex->pprev;
    }
    
}



void interruptableReceive(CPbftSharding& pbftShardingObj){
    // Placeholder: broadcast myself pubkey, and request others' pubkey.
//    pbftShardingObj.broadcastPubKey();
//    pbftShardingObj.broadcastPubKeyReq(); // request peer publickey
    struct sockaddr_in src_addr; // use this stuct to get sender IP and port
    size_t len = sizeof(src_addr);
    
    while(!ShutdownRequested()){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftShardingObj.udpServer->timed_recv(pbftShardingObj.pRecvBuf.get(), Message::messageSizeBytes, 500, &src_addr, &len);
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}
	
	switch(pbftShardingObj.pRecvBuf.get()[0]){
//	    case CPbft::pubKeyReqHeader:
//		// received msg is pubKeyReq. send pubKey
//		std::cout << "receive pubKey req" << std::endl;
//		// send public key to the peer.
//		pbftShardingObj.sendPubKey(src_addr, deserializePublicKeyReq(pbftShardingObj.pRecvBuf.get(), recvBytes));
//		continue;
//	    case CPbft::pubKeyMsgHeader:
//		deSerializePubKeyMsg(pbftShardingObj.peers, pbftShardingObj.pRecvBuf.get(), recvBytes, src_addr);
//		continue;
	    case CPbftSharding::clientReqHeader:
		// received client request, send preprepare
#ifdef BASIC_PBFT
		std::cout << "receive client req" << std::endl;
#endif
		uint32_t seq = pbftShardingObj.nextSeq++; 
		const char* reqCharBegin= &pbftShardingObj.pRecvBuf.get()[2];
		const char* reqCharEnd = &pbftShardingObj.pRecvBuf.get()[recvBytes];
		CDataStream txnMsg(reqCharBegin, reqCharEnd, SER_NETWORK, SERIALIZE_TRANSACTION_NO_WITNESS);
		PrePrepareMsg pp = pbftShardingObj.assemblePre_prepare(seq, txnMsg);
		pbftShardingObj.broadcast(&pp);
		continue;
	}
	
	
	// received msg is a PbftMessage.	
	std::string recvString(pbftShardingObj.pRecvBuf.get(), recvBytes);
	std::istringstream iss(recvString);
	int phaseNum = -1;
	iss >> phaseNum;
	switch(static_cast<PbftShardingPhase>(phaseNum)){
	    case PRE_PREPARE:
	    {
		PrePrepareMsg ppMsg(pbftShardingObj.server_id);
#ifdef SOCKET
		std::cout << "recvBytes = " << recvBytes << std::endl;
#endif
		ppMsg.deserialize(iss);
		pbftShardingObj.onReceivePrePrepare(ppMsg);
		break;
	    }
	    case PREPARE:
	    {
		Message pMsg(PbftShardingPhase::PREPARE, pbftShardingObj.server_id);
		pMsg.deserialize(iss);
		pbftShardingObj.onReceivePrepare(pMsg, true);
		break;
	    } 
	    case COMMIT:
	    {
		Message cMsg(PbftShardingPhase::COMMIT, pbftShardingObj.server_id);
		cMsg.deserialize(iss);
		pbftShardingObj.onReceiveCommit(cMsg, true);
		break;
	    }
	    default:
		std::cout << "received invalid msg" << std::endl;
		
	}
    }
}


void CPbftSharding::start(){
    receiver = std::thread(interruptableReceive, std::ref(*this)); 
    receiver.join();
}

bool CPbftSharding::onReceivePrePrepare(PrePrepareMsg& pre_prepare){
#ifdef BASIC_PBFT
    std::cout<< "server " << server_id << "received pre-prepare, seq = " << pre_prepare.seq << std::endl;
#endif
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if(!checkMsg(&pre_prepare)){
	return false;
    }
    // add to log
    log[pre_prepare.seq].pre_prepare = pre_prepare;
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */
    Message p = assembleMsg(PbftShardingPhase::PREPARE, pre_prepare.seq); 
    broadcast(&p); 
    if(log[pre_prepare.seq].prepareCount >= (nFaulty << 1)){
	log[pre_prepare.seq].phase = PbftShardingPhase::COMMIT;
	Message c = assembleMsg(PbftShardingPhase::COMMIT, pre_prepare.seq);
	broadcast(&c);
    } else {
#ifdef BASIC_PBFT
	std::cout << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
#endif
	log[pre_prepare.seq].phase = PbftShardingPhase::PREPARE;
    }
    return true;
}

bool CPbftSharding::onReceivePrepare(Message& prepare, bool sanityCheck){
#ifdef BASIC_PBFT
    std::cout << "server " <<server_id << " received prepare. seq = "  << prepare.seq << std::endl;
#endif
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&prepare)){
	return false;
    }
    
    //-----------add to log (currently use placeholder: should add the entire message to log and not increase re-count if the sender is the same. Also, if prepares are received earlier than pre-prepare, different prepare may have different digest. Should categorize buffered prepares based on digest.)
    //    log[prepare.seq].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
    if(log[prepare.seq].phase == PbftShardingPhase::PREPARE && log[prepare.seq].prepareCount == (nFaulty << 1)){
	// enter commit phase
#ifdef BASIC_PBFT
	std::cout << "server " << server_id << " enter commit phase" << std::endl;
#endif
	log[prepare.seq].phase = PbftShardingPhase::COMMIT;
	Message c = assembleMsg(PbftShardingPhase::COMMIT, prepare.seq); 
	broadcast(&c);
	return true;
    }
    return true;
}

bool CPbftSharding::onReceiveCommit(Message& commit, bool sanityCheck){
#ifdef BASIC_PBFT
    std::cout << "server " << server_id << "received commit, seq = "  << commit.seq  << std::endl;
#endif
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&commit)){
	return false;
    }
    
    // count the number of prepare msg. enter execute if greater than 2f+1
    log[commit.seq].commitCount++;
    if(log[commit.seq].phase == PbftShardingPhase::COMMIT && log[commit.seq].commitCount == (nFaulty << 1 ) + 1 ){ 
	// enter execute phase
#ifdef BASIC_PBFT
	std::cout << "enter reply phase" << std::endl;
#endif
	log[commit.seq].phase = PbftShardingPhase::REPLY;
	executeTransaction(commit.seq);
	return true;
    }

    //TODO: check if this transaction is a cross-shard one. If so, multicast commit or abort certificate and wait for other partitions decision if we vote commit.

    return true;
}

bool CPbftSharding::checkMsg(Message* msg){
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
    // server should be in the view
    if(localView != msg->view){
	std::cerr<< "server view = " << localView << ", but msg view = " << msg->view << std::endl;
	return false;
    }
    
    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if(!log[msg->seq].pre_prepare.digest.IsNull() && log[msg->seq].pre_prepare.digest != msg->digest){
	std::cerr<< "digest error for log entry " << msg->seq << ". digest in log = " << log[msg->seq].pre_prepare.digest.GetHex() << ", but msg->digest = " << msg->digest.GetHex() << std::endl;
	return false;
    }
    
    // if phase is pre-prepare, check if the digest matches client req
    if(msg->phase == PbftShardingPhase::PRE_PREPARE){
	if(msg->digest != (((PrePrepareMsg*)msg)->clientReq)->GetHash()){
	    std::cerr<< "digest does not match client request. Client req hash = " << ((PrePrepareMsg*)msg)->clientReq->GetHash().GetHex() << ", but digest = " << msg->digest.GetHex() << std::endl;
	    return false;
	}
    }
    
    // if phase is prepare or commit, also need to check view 
    if(msg->phase == PbftShardingPhase::PREPARE || msg->phase == PbftShardingPhase::COMMIT){
	
	if(log[msg->seq].pre_prepare.view != msg->view){
	    std::cerr<< "log entry view = " << log[msg->seq].pre_prepare.view << ", but msg view = " << msg->view << std::endl;
	    return false;
	}
    }
    return true;
}

PrePrepareMsg CPbftSharding::assemblePre_prepare(uint32_t seq, CDataStream& txnMsg){
#ifdef MSG_ASSEMBLE
    std::cout << "assembling pre_prepare, seq = " << seq << "client req = " << clientReq << std::endl;
#endif
    PrePrepareMsg toSent(server_id); // phase is set to Pre_prepare by default.
    toSent.seq = seq;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = toSent.clientReq->GetHash();
    txnMsg >> toSent.clientReq;
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so clientReq is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

// TODO: the real param should include digest, i.e. the block header hash.----(currently use placeholder)
Message CPbftSharding::assembleMsg(PbftShardingPhase phase, uint32_t seq){
    Message toSent(log[seq].pre_prepare, server_id);
    toSent.phase = phase;
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

Reply CPbftSharding::assembleReply(uint32_t seq){
    /* we use the digest from globalCC rather than digest from pre-prepare because 
     * a group may get a globalCC without GC msg from its own group.
     */
    // the last field of a requst is the timestamp
    // TODO: update reply msg to reflect the execution result of the transaction
    Reply toSent(seq, server_id, log[seq].result, log[seq].pre_prepare.digest, "m");
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

void CPbftSharding::broadcast(Message* msg){
    std::ostringstream oss;
    if(msg->phase == PbftShardingPhase::PRE_PREPARE){
	(static_cast<PrePrepareMsg*>(msg))->serialize(oss); 
    } else {
	msg->serialize(oss); 
    }
    // loop to  send prepare to all nodes in the peers map.
    for(auto p: peers){
	udpClient.sendto(oss, p.second.ip, p.second.port);
    }
    // virtually send the message to this node itself if it is a prepare or commit msg.
    switch(msg->phase){
	case PbftShardingPhase::PRE_PREPARE:
	    // do call onReceivePrePrepare, because the leader do not send prepare.
	    if(log[msg->seq].pre_prepare.digest.IsNull()){
		// add to log, phase is  auto-set to prepare
		log[msg->seq] = CLogEntry(*(static_cast<PrePrepareMsg*>(msg)));
#ifdef BASIC_PBFT
		std::cout<< "add to log, clientReq =" << (static_cast<PrePrepareMsg*>(msg))->clientReq << std::endl;
#endif
	    }
	    
	    break;
	case PbftShardingPhase::PREPARE:
	    onReceivePrepare(const_cast<Message&>(*msg), false);
	    break;
	case PbftShardingPhase::COMMIT:
	    onReceiveCommit(const_cast<Message&>(*msg), false);
	    break;
	default:
	    break;
    }
}

void CPbftSharding::executeTransaction(const int seq){
    // execute all lower-seq tx until this one if possible.
    int i = lastExecutedIndex + 1;
    for(; i < seq + 1; i++){
	if(log[i].phase == PbftShardingPhase::REPLY){
	    /* client request message format: "r <request type>,<key>[,<value>]". 
	     * Request type can be either read 'r' or write 'w'. If it is a 
	     * write request, client must provide value. We use comma as delimiter 
	     * here to avoid coflict with message deserialization, which use space
	     * as delimiter.
	     */
	    CTransaction req = *log[i].pre_prepare.clientReq; 
	    
//	    // TODO:change execution logic to update coins
//	    if(req.at(0) == 'w'){
//		// this is a write request
//		std::size_t found = req.find(',', 2);
//		int key = std::stoi(req.substr(2, found - 2));
//		data[key] = req.at(found + 1);
//		log[i].result = '0';  // '0' means write succeed. 
//#ifdef EXECUTION
//		std::cout << "-----server " << server_id << " write key: " << key << ", value :" 
//			<< data[key] << std::endl;
//#endif
//	    } else if(req.at(0) == 'r') {
//		/* Empty string means read failed because all writes come together with 
//		 * a write value and empty string simply means the key has never been 
//		 * inserted into the map.
//		 */
//		int key = std::stoi(req.substr(2));
//		log[i].result = data[key];  
//#ifdef EXECUTION
//		std::cout << "-----server " << server_id << " read key: " << key << ", value :" 
//			<< data[key] << std::endl;
//#endif
//	    } else {
//		std::cout << "invalid request" << std::endl;
//	    }
	    /* send reply right after execution. */
	    sendReply2Client(i);
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

void CPbftSharding::sendReply2Client(const int seq){
    std::ostringstream oss;
    Reply r = assembleReply(seq); 
    r.serialize(oss);
    // hard code client address for now.
    std::string clientIP = "127.0.0.1";
    int clientUdpPort = 12345; 
    udpClient.sendto(oss, clientIP, clientUdpPort);
#ifdef REPLY
    std::cout << "have sent reply to client port " << clientUdpPort << std::endl;
#endif
}

//void CPbft::broadcastPubKey(){
//    std::ostringstream oss;
//    // opti: serialized version can be stored.
//    serializePubKeyMsg(oss, server_id, udpServer->get_port(), publicKey);
//    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
//    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
//}
//
//
//void CPbft::sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id){
//    std::ostringstream oss;
//    serializePubKeyMsg(oss, server_id, udpServer->get_port(), publicKey);
//    udpClient.sendto(oss, inet_ntoa(src_addr.sin_addr), peers.at(recver_id).port);
//}
//
//
//void CPbft::broadcastPubKeyReq(){
//    std::ostringstream oss;
//    oss << pubKeyReqHeader;
//    oss << " ";
//    oss << server_id;
//    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
//    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
//}

CPubKey CPbftSharding::getPublicKey(){
    return publicKey;
}