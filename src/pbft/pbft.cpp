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
CPbft::CPbft(int serverPort): localView(0), globalView(0), log(std::vector<CPbftLogEntry>(CPbft::logSize)), members(std::vector<CService>(groupSize)), nGroups(1), udpServer(UdpServer("localhost", serverPort)), udpClient(UdpClient()), privateKey(CKey()){
    nFaulty = (members.size() - 1)/3;
    std::cout << "CPbft constructor. faulty nodes in a group =  "<< nFaulty << std::endl;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    pRecvBuf = new char[CPbftMessage::messageSizeBytes];
    std::cout << "publicKey = " << publicKey.GetHash().ToString() <<std::endl;
    srand(time(0));
    server_id = rand() % 100;
    peerPubKeys.insert(std::make_pair(server_id, publicKey));
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
void deSerializePubKeyMsg(std::unordered_map<uint32_t, CPubKey>& map, char* pRecvBuf, ssize_t recvBytes);

void interruptableReceive(CPbft& pbftObj){
    while(!ShutdownRequested()){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftObj.udpServer.timed_recv(pbftObj.pRecvBuf, CPbftMessage::messageSizeBytes, 500);
	
	if(recvBytes == -1 &&  pbftObj.peerPubKeys.empty()){
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
	    deSerializePubKeyMsg(pbftObj.peerPubKeys, pbftObj.pRecvBuf, recvBytes);
	    
	    // ----------- placeholder:send dummy preprepare
	    if(pbftObj.log[1].prepareCount == 0){
		pbftObj.broadcast(pbftObj.assemblePre_prepare(1));
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
    // verify signature and return wrong if sig is wrong
    uint256 msgHash;
    pre_prepare.getHash(msgHash);
    if(peerPubKeys.find(pre_prepare.senderId) != peerPubKeys.end() &&  !peerPubKeys[pre_prepare.senderId].Verify(msgHash, pre_prepare.vchSig)){
	std::cerr<< "verification fail" << std::endl;
    	return false;
    } 
    std::cout << "verify pre-prepare succeed" << std::endl;
    
    // assume sigs are all good, so the protocol enters prepare phase.
    std::cout << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
    // add to log
    log[pre_prepare.seq] = CPbftLogEntry(pre_prepare);
    log[pre_prepare.seq].phase = PbftPhase::prepare;
    broadcast(assembleMsg(PbftPhase::prepare, pre_prepare.seq));
    std::cout << " seq = " << pre_prepare.seq << ", prepareCount = " << log[pre_prepare.seq].prepareCount << std::endl;
    
    
    return true;
}

bool CPbft::onReceivePrepare(CPbftMessage& prepare){
    std::cout << "received prepare." << std::endl;
    //verify sig. if wrong, return false.
    uint256 msgHash;
    prepare.getHash(msgHash);
    if(peerPubKeys.find(prepare.senderId) != peerPubKeys.end() &&  !peerPubKeys[prepare.senderId].Verify(msgHash, prepare.vchSig)){
	std::cerr<< "prepare msg: verification fail" << std::endl;
    	return false;
    } 
    std::cout << "verify prepare succeed" << std::endl;
    // placeholder :also check if view number is the same
    
    //-----------add to log (currently use placeholder)
    //    log[prepare.seq].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareCount == 1 ){ //placeholder for (nFaulty << 1)
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
    //verify sig. if wrong, return false.
    uint256 msgHash;
    commit.getHash(msgHash);
    if(peerPubKeys.find(commit.senderId) != peerPubKeys.end() &&  !peerPubKeys[commit.senderId].Verify(msgHash, commit.vchSig)){
	std::cerr<< "commit msg: verification fail" << std::endl;
    	return false;
    } 
    std::cout << "verify commit succeed" << std::endl;
    // placeholder :also check if view number is the same
    
    //-----------add to log (currently use placeholder)
    //    log[commit.seq].commitArray.push_back(commit);
    
    // count the number of prepare msg. enter reply if greater than 2f+1
    log[commit.seq].prepareCount++;
    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitCount == 2 ){ //placeholder for (nFaulty << 1 ) + 1
	// enter commit phase
	std::cout << "enter reply phase" << std::endl;
	log[commit.seq].phase = PbftPhase::reply;
	excuteTransactions(commit.digest);
	return true;
    }
    return true;
}


CPbftMessage CPbft::assemblePre_prepare(uint32_t seq){
    std::cout << "assembling pre_prepare" << std::endl;
    CPbftMessage toSent(server_id);
    toSent.seq = seq;
    toSent.view = 5;
    // TODO: set the digest value as the hash of a block or a transaction.
    uint256 hash;
    toSent.getHash(hash);
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

void deSerializePubKeyMsg(std::unordered_map<uint32_t, CPubKey>& map, const char* pRecvBuf, const ssize_t recvBytes){
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
    std::cout << "received publicKey = " << pk.GetHash().ToString() <<std::endl;
    map.insert(std::make_pair(senderId, pk));
}