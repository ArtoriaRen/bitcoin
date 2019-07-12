/*
 * to change this license header, choose license headers in project properties.
 * to change this template file, choose tools | templates
 * and open the template in the editor.
 */

// TODO: buffer future prepare and commit.



#include <locale>

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

void interruptableReceive(CPbft& pbftObj){
    while(!ShutdownRequested()){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftObj.udpServer.timed_recv(pbftObj.pRecvBuf, CPbftMessage::messageSizeBytes, 500);
	if(recvBytes < 3){
	    // received msg is pubKeyReq. send pubKey
	    pbftObj.broadcastPubKey(pbftObj.publicKey);
	    continue;
	}
	
	std::string recvString(pbftObj.pRecvBuf, recvBytes);

	if(pbftObj.pRecvBuf[0] == 'a'){
	    // received msg is a public key of peers.
	    std::istringstream iss(recvString.substr(2)); //construct a stream start from index 2, because the first two chars ('a' and ' ') are not part of  a public key. 
	    CPubKey pk;
	    pk.Unserialize(iss);
	}
	
	// request peer publickey
	if(pbftObj.peerPubKeys.empty()){
	    pbftObj.broadcastPubKeyReq();
	    continue;
	}
	
	
	
	// recvBytes should be greater than 5 to fill all fields of a PbftMessage object.
	if( recvBytes > 5){
	    std::istringstream iss(recvString);
	    CPbftMessage recvMsg;
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





bool CPbft::onReceivePrePrepare(const CPbftMessage& pre_prepare){
    
    std::cout<< "received pre-prepare" << std::endl;
    // verify signature and return wrong if sig is wrong
    //    if(! verify(sig)){
    //	return false;
    //    }
    
    // assume sigs are all good, so the protocol enters prepare phase.
    std::cout << "enter prepare phase. seq in pre-prepare = " << pre_prepare.seq << std::endl;
    // add to log
    log[pre_prepare.seq] = CPbftLogEntry(pre_prepare);
    log[pre_prepare.seq].phase = PbftPhase::prepare;
    broadcast(assembleMsg(PbftPhase::prepare, pre_prepare.seq));
    log[pre_prepare.seq].prepareCount++; // add one since the node iteself send prepare.
    
    
    return true;
}

bool CPbft::onReceivePrepare(const CPbftMessage& prepare){
    std::cout << "received prepare. seq in prepare = " << prepare.seq << ", Phase  = " << log[prepare.seq].phase << std::endl;
    //verify sig. if wrong, return false.
    
    
    //-----------add to log (currently use placeholder)
    //    log[prepare.seq].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    //    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareArray.size() >= (nFaulty << 1) ){
    log[prepare.seq].prepareCount++;
    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareCount == (nFaulty << 1) ){
	// enter commit phase
	std::cout << "enter commit phase" << std::endl;
	log[prepare.seq].phase = PbftPhase::commit;
	broadcast(assembleMsg(PbftPhase::commit, prepare.seq));
	log[prepare.seq].commitCount++;// add one since the node iteself send prepare.
	return true;
    }
    return true;
}

bool CPbft::onReceiveCommit(const CPbftMessage& commit){
    std::cout << "received commit" << std::endl;
    //verify sig. if wrong, return false.
    
    //-----------add to log (currently use placeholder)
    //    log[commit.seq].commitArray.push_back(commit);
    // count the number of prepare msg. enter reply if greater than 2f+1
    //    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitArray.size() >= (nFaulty << 1 ) + 1 ){
    log[commit.seq].commitCount++;
    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitCount >= (nFaulty << 1 ) + 1 ){
	// enter commit phase
	std::cout << "enter reply phase" << std::endl;
	log[commit.seq].phase = PbftPhase::reply;
	excuteTransactions(commit.digest);
	return true;
    }
    return true;
}

// TODO: the real param should include digest, i.e. the block header hash.----(currently use placeholder)
CPbftMessage CPbft::assembleMsg(PbftPhase phase, uint32_t seq){
    CPbftMessage toSent(log[seq].pre_prepare);
    toSent.phase = phase;
    return toSent;
}

void CPbft::broadcast(const CPbftMessage& msg){
    // send prepare to all nodes in the members array.
    std::cout << "sending phase =" << msg.phase << std::endl; 
    std::ostringstream oss;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    msg.serialize(oss); 
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}

void CPbft::excuteTransactions(const uint256& digest){
    std::cout << "executing tx..." << std::endl;
}

void CPbft::broadcastPubKey(const CPubKey& pk){
    std::ostringstream oss;
    oss << pubKeyMsgHeader;
    oss << " ";
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    pk.Serialize(oss); 
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}


void CPbft::broadcastPubKeyReq(){
    std::ostringstream oss;
    oss << CPbft::pubKeyReqHeader;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}