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

//----------placeholder:members is initialized as size-4.

CPbft::CPbft(int serverPort, unsigned int id): localView(0), globalView(0), log(std::vector<CPbftLogEntry>(CPbft::logSize)), nextSeq(0), members(std::vector<uint32_t>(groupSize)), server_id(id), nGroups(1), udpServer(UdpServer("localhost", serverPort)), udpClient(UdpClient()), privateKey(CKey()), x(-1){
    nFaulty = (members.size() - 1)/3;
    std::cout << "CPbft constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    pRecvBuf = new char[CPbftMessage::messageSizeBytes];
    CPbftPeer myself("localhost", serverPort, publicKey); 
    peers.insert(std::make_pair(server_id, myself));
    std::cout << "my serverId = " << server_id << ", publicKey = " << publicKey.GetHash().ToString() <<std::endl;
}


CPbft::~CPbft(){
    delete []pRecvBuf;
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


//------------------ funcs run in udp server thread-------------
void serializePubKeyMsg(std::ostringstream& oss, uint32_t senderId, const CPubKey& pk);
uint32_t deSerializePubKeyMsg(std::unordered_map<uint32_t, CPbftPeer>& map, char* pRecvBuf, ssize_t recvBytes, const struct sockaddr_in& src_addr);

void interruptableReceive(CPbft& pbftObj){
    // Placeholder: broadcast myself pubkey, and request others' pubkey.
    pbftObj.broadcastPubKey();
    pbftObj.broadcastPubKeyReq(); // request peer publickey
    struct sockaddr_in src_addr; // use this stuct to get sender IP and port
    size_t len = sizeof(src_addr);
    
    while(!ShutdownRequested()){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftObj.udpServer.timed_recv(pbftObj.pRecvBuf, CPbftMessage::messageSizeBytes, 500, &src_addr, &len);
	
	//	if(recvBytes == -1 &&  pbftObj.peerPubKeys.size() == 1){
	//	    // timeout or error occurs.
	//	    pbftObj.broadcastPubKeyReq(); // request peer publickey
	//	    continue; 
	//	}
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}
	
	switch(pbftObj.pRecvBuf[0]){
	    case CPbft::pubKeyReqHeader:
		// received msg is pubKeyReq. send pubKey
		std::cout << "receive pubKey req" << std::endl;
		pbftObj.broadcastPubKey();
		continue;
	    case CPbft::pubKeyMsgHeader:
		deSerializePubKeyMsg(pbftObj.peers, pbftObj.pRecvBuf, recvBytes, src_addr);
		continue;
	    case CPbft::clientReqHeader:
		// received client request, send preprepare
		uint32_t seq = pbftObj.nextSeq++; 
		std::string clientReq(&pbftObj.pRecvBuf[2], recvBytes - 2);
		CPre_prepare pp = pbftObj.assemblePre_prepare(seq, clientReq);
		pbftObj.broadcast(&pp);
		// placeholder : send msg back to client
		std::ostringstream oss;
		oss << pbftObj.log[0].pre_prepare.clientReq;
		std::cout<< "send back to client: " <<pbftObj.log[0].pre_prepare.clientReq << std::endl;
		pbftObj.udpClient.sendto(oss, "localhost", 18500);
		continue;
	}
	
	
	// received msg is either a client req or a PbftMessage.	
	std::string recvString(pbftObj.pRecvBuf, recvBytes);
	std::istringstream iss(recvString);
	int phaseNum = -1;
	iss >> phaseNum;
	switch(static_cast<PbftPhase>(phaseNum)){
	    case pre_prepare:
	    {
		CPre_prepare ppMsg(pbftObj.server_id);
		ppMsg.deserialize(iss);
		pbftObj.onReceivePrePrepare(ppMsg);
		break;
	    }
	    case prepare:
	    {
		CPbftMessage pMsg(pbftObj.server_id);
		pMsg.deserialize(iss);
		pbftObj.onReceivePrepare(pMsg, true);
		break;
	    } 
	    case commit:
	    {
		CPbftMessage cMsg(pbftObj.server_id);
		cMsg.deserialize(iss);
		pbftObj.onReceiveCommit(cMsg, true);
		break;
	    }
	    case reply:
		// only the local leader need to handle the reply message?
		std::cout << "received reply msg" << std::endl;
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

bool CPbft::onReceivePrePrepare(CPbftMessage& pre_prepare){
    
    std::cout<< "received pre-prepare" << std::endl;
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if(!checkMsg(pre_prepare)){
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
    if(sanityCheck && !checkMsg(prepare)){
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
    if(sanityCheck && !checkMsg(commit)){
	return false;
    }
    
    //-----------add to log (currently use placeholder)
    //    log[commit.seq].commitArray.push_back(commit);
    
    // count the number of prepare msg. enter reply if greater than 2f+1
    log[commit.seq].commitCount++;
    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitCount == (nFaulty << 1 ) + 1 ){ 
	// enter commit phase
	std::cout << "enter reply phase" << std::endl;
	log[commit.seq].phase = PbftPhase::reply;
	excuteTransactions(commit.digest);
	return true;
    }
    return true;
}

bool CPbft::checkMsg(CPbftMessage& msg){
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
    // server should be in the view
    if(localView != msg.view){
	std::cerr<< "server view = " << localView << ", but msg view = " << msg.view << std::endl;
	return false;
    }
    
    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if(!log[msg.seq].pre_prepare.digest.IsNull() && log[msg.seq].pre_prepare.digest != msg.digest){
	std::cerr<< "digest error. digest in log = " << log[msg.seq].pre_prepare.digest.GetHex() << ", but msg.digest = " << msg.digest.GetHex() << std::endl;
	return false;
    }
    
    // if phase is prepare or commit, also need to check view  and digest value.
    if(msg.phase == PbftPhase::prepare || msg.phase == PbftPhase::commit){
	
	if(log[msg.seq].pre_prepare.view != msg.view){
	    std::cerr<< "log entry view = " << log[msg.seq].pre_prepare.view << ", but msg view = " << msg.view << std::endl;
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
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.clientReq = clientReq;
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
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    msg->serialize(oss); 
    // placeholder: loop to  send prepare to all nodes in the members array.
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
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

void CPbft::excuteTransactions(const uint256& digest){
    std::cout << "executing tx..." << std::endl;
    
}

void CPbft::broadcastPubKey(){
    std::ostringstream oss;
    serializePubKeyMsg(oss, server_id, publicKey);
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}


void CPbft::broadcastPubKeyReq(){
    std::ostringstream oss;
    oss << pubKeyReqHeader;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}


void serializePubKeyMsg(std::ostringstream& oss, const uint32_t senderId, const CPubKey& pk){
    oss << CPbft::pubKeyMsgHeader;
    oss << " ";
    oss << senderId;
    oss << " ";
    pk.Serialize(oss); 
}

uint32_t deSerializePubKeyMsg(std::unordered_map<uint32_t, CPbftPeer>& map, char* pRecvBuf, const ssize_t recvBytes, const struct sockaddr_in& src_addr){
    std::string recvString(pRecvBuf, recvBytes);
    std::istringstream iss(recvString); //construct a stream start from index 2, because the first two chars ('a' and ' ') are not part of  a public key. 
    char msgHeader;
    uint32_t senderId; 
    CPubKey pk;
    iss >> msgHeader;
    iss >> senderId;
    iss.get();
    pk.Unserialize(iss);
    std::cout << "received publicKey = (" << senderId << ", " << pk.GetHash().ToString() << ")"<<std::endl;
    // extract peer ip and port
    std::string ip(inet_ntoa(src_addr.sin_addr));
    CPbftPeer peer(ip, src_addr.sin_port, pk);
    map.insert(std::make_pair(senderId, peer));
    return senderId;
}



CPubKey CPbft::getPublicKey(){
    return publicKey;
}