/*
 * to change this license header, choose license headers in project properties.
 * to change this template file, choose tools | templates
 * and open the template in the editor.
 */

// TODO: buffer future prepare and commit.



#include <locale>
#include<string.h>

#include "pbft/pbft.h"
#include "init.h"
#include "pbft/pbft_msg.h"

//----------placeholder:members is initialized as size-4.
CPbft::CPbft(int serverPort): localView(0), globalView(0), log(std::vector<CPbftLogEntry>(CPbft::logSize)), nextSeq(0), members(std::vector<CService>(groupSize)), nGroups(1), udpServer(UdpServer("localhost", serverPort)), udpClient(UdpClient()), privateKey(CKey()){
    nFaulty = (members.size() - 1)/3;
    std::cout << "CPbft constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    pRecvBuf = new char[CPbftMessage::messageSizeBytes];
    srand(time(0));
    server_id = rand() % 100;
    peerPubKeys.insert(std::make_pair(server_id, publicKey));
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
uint32_t deSerializePubKeyMsg(std::unordered_map<uint32_t, CPubKey>& map, char* pRecvBuf, ssize_t recvBytes);

void interruptableReceive(CPbft& pbftObj){
    while(!ShutdownRequested()){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftObj.udpServer.timed_recv(pbftObj.pRecvBuf, CPbftMessage::messageSizeBytes, 500);
	
	if(recvBytes == -1 &&  pbftObj.peerPubKeys.size() == 1){
	    // timeout or error occurs.
	    pbftObj.broadcastPubKeyReq(); // request peer publickey
	    continue; 
	}
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}
	
	if(pbftObj.pRecvBuf[0] == CPbft::pubKeyReqHeader){
	    // received msg is pubKeyReq. send pubKey
	    std::cout << "receive pubKey req" << std::endl;
	    pbftObj.broadcastPubKey();
	    continue;
	}
	
	if(pbftObj.pRecvBuf[0] == CPbft::pubKeyMsgHeader){
	    uint32_t recvSenderId = deSerializePubKeyMsg(pbftObj.peerPubKeys, pbftObj.pRecvBuf, recvBytes);
	    
	    // ----------- placeholder:send dummy preprepare
	    if(pbftObj.server_id < recvSenderId){
	    	pbftObj.broadcastPubKey(); // send public key again in case other peers do not know our publickey .
		uint32_t seq = rand() % CPbft::logSize; // placeholder : should use the next seq.
		pbftObj.broadcast(pbftObj.assemblePre_prepare(seq, "test"));
	    }
	    continue;
	}
	
	// recvBytes should be greater than 5 to fill all fields of a PbftMessage object.
	if( recvBytes > 5){
	    std::string recvString(pbftObj.pRecvBuf, recvBytes);
	    std::istringstream iss(recvString);
	    CPbftMessage recvMsg(pbftObj.server_id);
	    recvMsg.deserialize(iss);
	    switch(recvMsg.phase){
		case pre_prepare:
		    pbftObj.onReceivePrePrepare(recvMsg);
		    break;
		case prepare:
		    pbftObj.onReceivePrepare(recvMsg);
		    break;
		case commit:
		    pbftObj.onReceiveCommit(recvMsg);
		    break;
		case reply:
		    // only the local leader need to handle the reply message?
		    std::cout << "received reply msg" << std::endl;
		    break;
		default:
		    std::cout << "received invalid msg" << std::endl;
		    
	    }
	} 
    }
}


void CPbft::start(){
    receiver = std::thread(interruptableReceive, std::ref(*this)); 
    receiver.join();
}

bool CPbft::onReceivePrePrepare(CPbftMessage& pre_prepare){
    
    std::cout<< "received pre-prepare" << std::endl;
    // sanity check for signature, seq, view.
    if(!checkMsg(pre_prepare)){
	return false;
    }
    std::cout << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
    // add to log
    log[pre_prepare.seq] = CPbftLogEntry(pre_prepare);
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */
    if(log[pre_prepare.seq].prepareCount >= (nFaulty << 1)){
    log[pre_prepare.seq].phase = PbftPhase::commit;
    broadcast(assembleMsg(PbftPhase::prepare, pre_prepare.seq)); // also need to send prepare so that other servers can collect enough prepares.
    broadcast(assembleMsg(PbftPhase::commit, pre_prepare.seq));
    } else {
    log[pre_prepare.seq].phase = PbftPhase::prepare;
    broadcast(assembleMsg(PbftPhase::prepare, pre_prepare.seq));
    }
    return true;
}

bool CPbft::onReceivePrepare(CPbftMessage& prepare){
    std::cout << "received prepare." << std::endl;
    // sanity check for signature, seq, view.
    if(!checkMsg(prepare)){
	return false;
    }
    
    //-----------add to log (currently use placeholder)
    //    log[prepare.seq].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareCount == (nFaulty << 1)){
	// enter commit phase
	std::cout << "enter commit phase" << std::endl;
	log[prepare.seq].phase = PbftPhase::commit;
	broadcast(assembleMsg(PbftPhase::commit, prepare.seq));
	return true;
    }
    return true;
}

bool CPbft::onReceiveCommit(CPbftMessage& commit){
    std::cout << "received commit" << std::endl;
    // sanity check for signature, seq, view.
    if(!checkMsg(commit)){
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
    if(peerPubKeys.find(msg.senderId) == peerPubKeys.end()){
	std::cerr<< "no pub key for the sender" << std::endl;
	return false;
    }
    uint256 msgHash;
    msg.getHash(msgHash);
    if(!peerPubKeys[msg.senderId].Verify(msgHash, msg.vchSig)){
	std::cerr<< "verification sig fail" << std::endl;
    	return false;
    } 
    // server should be in the view
    if(localView != msg.view){
	std::cerr<< "server view = " << localView << ", but msg view = " << msg.view << std::endl;
	return false;
    }
    
    /* check if the seq is alreadly attached to another digest.
     * Faulty followers may accept.
     */
    if(!log[msg.seq].pre_prepare.digest.IsNull() && log[msg.seq].pre_prepare.digest != msg.digest){
	std::cerr<< "digest error. digest in log = " << log[msg.seq].pre_prepare.digest.GetHex() << ", but msg.digest = " << msg.digest.GetHex() << std::endl;
	return false;
    }
    
    // if phase is prepare or commit, also need to check view  and digest value.
    if(msg.phase == PbftPhase::prepare || msg.phase == PbftPhase::commit){
	
	if(log[msg.seq].pre_prepare.view != msg.view){
	    std::cerr<< "log entry view = " << log[msg.seq].pre_prepare.view << ", but msg view = " << msg.view << std::endl;
	}
	
	if(log[msg.seq].pre_prepare.digest != msg.digest){
	    std::cerr<< "digest do not match" << std::endl;
	}
    }
    std::cout << "sanity check succeed" << std::endl;
    return true;
}

CPbftMessage CPbft::assemblePre_prepare(uint32_t seq, std::string clientReq){
    std::cout << "assembling pre_prepare" << std::endl;
    CPbftMessage toSent(server_id); // phase is set to Pre_prepare by default.
    toSent.seq = seq;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.digest = Hash(clientReq.begin(), clientReq.end());
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    /*Faulty leaders may change an existing log entry.*/
    if(log[seq].pre_prepare.digest.IsNull()){
	log[seq].pre_prepare = toSent;
    }
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

void CPbft::broadcast(const CPbftMessage& msg){
    std::ostringstream oss;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    msg.serialize(oss); 
    // placeholder: loop to  send prepare to all nodes in the members array.
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
    // virtually send the message to this node itself if it is a prepare or commit msg.
    if(msg.phase == PbftPhase::prepare){
	onReceivePrepare(const_cast<CPbftMessage&>(msg));
    } else if (msg.phase == PbftPhase::commit){
	onReceiveCommit(const_cast<CPbftMessage&>(msg));
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

uint32_t deSerializePubKeyMsg(std::unordered_map<uint32_t, CPubKey>& map, char* pRecvBuf, const ssize_t recvBytes){
    std::string recvString(pRecvBuf, recvBytes);
    //	    std::cout << "receive pubKey, string = " << recvString << ", string size = " << recvString.size() << ", deserializing..." << std::endl;
    std::istringstream iss(recvString); //construct a stream start from index 2, because the first two chars ('a' and ' ') are not part of  a public key. 
    char msgHeader;
    uint32_t senderId; 
    CPubKey pk;
    iss >> msgHeader;
    iss >> senderId;
    iss.get();
    pk.Unserialize(iss);
    std::cout << "received publicKey = (" << senderId << ", " << pk.GetHash().ToString() << ")"<<std::endl;
    map.insert(std::make_pair(senderId, pk));
    return senderId;
}