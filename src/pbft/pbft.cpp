/*
 * to change this license header, choose license headers in project properties.
 * to change this template file, choose tools | templates
 * and open the template in the editor.
 */

// TODO: buffer future prepare and commit.



#include <locale>
#include<string.h>
#include <netinet/in.h>
#include "pbft/pbft.h"
#include "init.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "pbft/peer.h"
#include "pbft/util.h"

//----------placeholder:members is initialized as size-4.

CPbft::CPbft(int serverPort, unsigned int id): localView(0), globalView(0), log(std::vector<CPbftLogEntry>(CPbft::logSize)), nextSeq(0), lastExecutedIndex(-1), members(std::vector<uint32_t>(groupSize)), server_id(id), nGroups(1), udpServer(UdpServer("localhost", serverPort)), udpClient(UdpClient()), pRecvBuf(new char[CPbftMessage::messageSizeBytes], std::default_delete<char[]>()), privateKey(CKey()), x(-1){
    nFaulty = (members.size() - 1)/3;
    std::cout << "CPbft constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    CPbftPeer myself("localhost", serverPort, publicKey); 
    //    peers.insert(std::make_pair(server_id, myself));
    std::cout << "my serverId = " << server_id << ", publicKey = " << publicKey.GetHash().ToString() <<std::endl;
}




/**
 * Go through the last nBlocks block, calculate membership of nGroups groups.
 * @param random is the random number used to group nodes.
 * @param nBlocks is number of blocks whose miner participate in the PBFT.
 * @return 
 */
void CPbft::group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindexNew) {
    const CBlockIndex* pindex = pindexNew; // make a copy so that we do not change the original argument passed in
    LogPrintf("group number %d nBlock = %d, pindex->nHeight = %d \n", nGroups, nBlocks, pindex->nHeight);
    for (uint i = 0; i < nBlocks && pindex != nullptr; i++) {
        //TODO: get block miner IP addr and port, add them to the members
        LogPrintf("pbft: block height = %d, ip&port = %s \n ", pindex->nHeight, pindex->netAddrPort.ToString());
        pindex = pindex->pprev;
    }
    
}



void interruptableReceive(CPbft& pbftObj){
    // Placeholder: broadcast myself pubkey, and request others' pubkey.
    pbftObj.broadcastPubKey();
    pbftObj.broadcastPubKeyReq(); // request peer publickey
    struct sockaddr_in src_addr; // use this stuct to get sender IP and port
    size_t len = sizeof(src_addr);
    
    while(!ShutdownRequested()){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftObj.udpServer.timed_recv(pbftObj.pRecvBuf.get(), CPbftMessage::messageSizeBytes, 500, &src_addr, &len);
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}
	
	switch(pbftObj.pRecvBuf.get()[0]){
	    case CPbft::pubKeyReqHeader:
		// received msg is pubKeyReq. send pubKey
		std::cout << "receive pubKey req" << std::endl;
		// send public key to the peer.
		pbftObj.sendPubKey(src_addr, deserializePublicKeyReq(pbftObj.pRecvBuf.get(), recvBytes));
		continue;
	    case CPbft::pubKeyMsgHeader:
		deSerializePubKeyMsg(pbftObj.peers, pbftObj.pRecvBuf.get(), recvBytes, src_addr);
		continue;
	    case CPbft::clientReqHeader:
		// received client request, send preprepare
		uint32_t seq = pbftObj.nextSeq++; 
		std::string clientReq(&pbftObj.pRecvBuf.get()[2], recvBytes - 2);
		CPre_prepare pp = pbftObj.assemblePre_prepare(seq, clientReq);
		pbftObj.broadcast(&pp);
		continue;
	}
	
	
	// received msg is either a client req or a PbftMessage.	
	std::string recvString(pbftObj.pRecvBuf.get(), recvBytes);
	std::istringstream iss(recvString);
	int phaseNum = -1;
	iss >> phaseNum;
	switch(static_cast<PbftPhase>(phaseNum)){
	    case pre_prepare:
	    {
		CPre_prepare ppMsg(pbftObj.server_id);
		std::cout << "recvBytes = " << recvBytes << std::endl;
		ppMsg.deserialize(iss);
		pbftObj.onReceivePrePrepare(ppMsg);
		break;
	    }
	    case prepare:
	    {
		CPbftMessage pMsg(PbftPhase::prepare, pbftObj.server_id);
		pMsg.deserialize(iss);
		pbftObj.onReceivePrepare(pMsg, true);
		break;
	    } 
	    case commit:
	    {
		CPbftMessage cMsg(PbftPhase::commit, pbftObj.server_id);
		cMsg.deserialize(iss);
		pbftObj.onReceiveCommit(cMsg, true);
		break;
	    }
	    case execute:
		// only the local leader need to handle the execute message?
		std::cout << "received execute msg" << std::endl;
		break;
	    default:
		std::cout << "received invalid msg" << std::endl;
		
	}
    }
}


void CPbft::start(){
    receiver = std::thread(interruptableReceive, std::ref(*this)); 
    receiver.join();
}

bool CPbft::onReceivePrePrepare(CPre_prepare& pre_prepare){
    
    std::cout<< "received pre-prepare" << std::endl;
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
    CPbftMessage p = assembleMsg(PbftPhase::prepare, pre_prepare.seq); 
    broadcast(&p); 
    if(log[pre_prepare.seq].prepareCount >= (nFaulty << 1)){
	log[pre_prepare.seq].phase = PbftPhase::commit;
	CPbftMessage c = assembleMsg(PbftPhase::commit, pre_prepare.seq);
	broadcast(&c);
    } else {
	std::cout << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
	log[pre_prepare.seq].phase = PbftPhase::prepare;
    }
    return true;
}

bool CPbft::onReceivePrepare(CPbftMessage& prepare, bool sanityCheck){
    std::cout << "received prepare." << std::endl;
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&prepare)){
	return false;
    }
    
    //-----------add to log (currently use placeholder: should add the entire message to log and not increase re-count if the sender is the same. Also, if prepares are received earlier than pre-prepare, different prepare may have different digest. Should categorize buffered prepares based on digest.)
    //    log[prepare.seq].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareCount == (nFaulty << 1)){
	// enter commit phase
	std::cout << "enter commit phase" << std::endl;
	log[prepare.seq].phase = PbftPhase::commit;
	CPbftMessage c = assembleMsg(PbftPhase::commit, prepare.seq); 
	broadcast(&c);
	return true;
    }
    return true;
}

bool CPbft::onReceiveCommit(CPbftMessage& commit, bool sanityCheck){
    std::cout << "received commit" << std::endl;
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&commit)){
	return false;
    }
    
    //-----------add to log (currently use placeholder)
    //    log[commit.seq].commitArray.push_back(commit);
    
    // count the number of prepare msg. enter execute if greater than 2f+1
    log[commit.seq].commitCount++;
    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitCount == (nFaulty << 1 ) + 1 ){ 
	// enter commit phase
	std::cout << "enter execute phase" << std::endl;
	log[commit.seq].phase = PbftPhase::execute;
	executeTransaction(commit.seq);
	return true;
    }
    return true;
}

bool CPbft::checkMsg(CPbftMessage* msg){
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
	std::cerr<< "digest error. digest in log = " << log[msg->seq].pre_prepare.digest.GetHex() << ", but msg->digest = " << msg->digest.GetHex() << std::endl;
	return false;
    }

    // if phase is pre-prepare, check if the digest matches client req
    if(msg->phase == PbftPhase::pre_prepare){
	std::string req = ((CPre_prepare*)msg)->clientReq;
	
	if(msg->digest != Hash(req.begin(), req.end())){
	    std::cerr<< "digest does not match client request. Client req = " << req << ", but digest = " << msg->digest.GetHex() << std::endl;
	    return false;
	    
	}
    }
    
    // if phase is prepare or commit, also need to check view  and digest value.
    if(msg->phase == PbftPhase::prepare || msg->phase == PbftPhase::commit){
	
	if(log[msg->seq].pre_prepare.view != msg->view){
	    std::cerr<< "log entry view = " << log[msg->seq].pre_prepare.view << ", but msg view = " << msg->view << std::endl;
	    return false;
	}
    }
    std::cout << "sanity check succeed" << std::endl;
    return true;
}

CPre_prepare CPbft::assemblePre_prepare(uint32_t seq, std::string clientReq){
    std::cout << "assembling pre_prepare, client req = " << clientReq << std::endl;
    CPre_prepare toSent(server_id); // phase is set to Pre_prepare by default.
    toSent.seq = seq;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = Hash(clientReq.begin(), clientReq.end());
    toSent.clientReq = clientReq;
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so clientReq is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

// TODO: the real param should include digest, i.e. the block header hash.----(currently use placeholder)
CPbftMessage CPbft::assembleMsg(PbftPhase phase, uint32_t seq){
    CPbftMessage toSent(log[seq].pre_prepare, server_id);
    toSent.phase = phase;
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    return toSent;
}

void CPbft::broadcast(CPbftMessage* msg){
    std::ostringstream oss;
    if(msg->phase == PbftPhase::pre_prepare){
	(static_cast<CPre_prepare*>(msg))->serialize(oss); 
    } else {
	msg->serialize(oss); 
    }
    // loop to  send prepare to all nodes in the peers map.
    for(auto p: peers){
	udpClient.sendto(oss, p.second.ip, p.second.port);
    }
    // virtually send the message to this node itself if it is a prepare or commit msg.
    switch(msg->phase){
	case PbftPhase::pre_prepare:
	    // do call onReceivePrePrepare, because the leader do not send prepare.
	    if(log[msg->seq].pre_prepare.digest.IsNull()){
		// add to log, phase is  auto-set to prepare
		log[msg->seq] = CPbftLogEntry(*(static_cast<CPre_prepare*>(msg)));
		std::cout<< "add to log, clientReq =" << (static_cast<CPre_prepare*>(msg))->clientReq << std::endl;
	    }
	    
	    break;
	case PbftPhase::prepare:
	    onReceivePrepare(const_cast<CPbftMessage&>(*msg), false);
	    break;
	case PbftPhase::commit:
	    onReceiveCommit(const_cast<CPbftMessage&>(*msg), false);
	    break;
	default:
	    break;
    }
}

void CPbft::executeTransaction(const int seq){
    // execute all lower-seq tx until this one if possible.
    int i = lastExecutedIndex + 1;
    for(; i < seq + 1; i++){
	if(log[seq].phase == PbftPhase::execute){
	    // send msg back to client. The client will wait for f+1 consistent responses.
	    std::ostringstream oss;
	    oss <<log[seq].pre_prepare.clientReq;
	    std::cout<< "executing and send back to client: " <<log[seq].pre_prepare.clientReq << std::endl;
	    udpClient.sendto(oss, "localhost", 18500);
	} else {
	    break;
	}
    }
    lastExecutedIndex = i-1;
    
}

void CPbft::broadcastPubKey(){
    std::ostringstream oss;
    // opti: serialized version can be stored.
    serializePubKeyMsg(oss, server_id, udpServer.get_port(), publicKey);
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}


void CPbft::sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id){
    std::ostringstream oss;
    serializePubKeyMsg(oss, server_id, udpServer.get_port(), publicKey);
    udpClient.sendto(oss, inet_ntoa(src_addr.sin_addr), peers.at(recver_id).port);
}


void CPbft::broadcastPubKeyReq(){
    std::ostringstream oss;
    oss << pubKeyReqHeader;
    oss << " ";
    oss << server_id;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}

CPubKey CPbft::getPublicKey(){
    return publicKey;
}