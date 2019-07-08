/*
 * to change this license header, choose license headers in project properties.
 * to change this template file, choose tools | templates
 * and open the template in the editor.
 */


#include <locale>

#include "pbft/pbft.h"
#include "init.h"
#include "pbft/pbft_msg.h"



CPbft::CPbft(int serverPort): udpServer(UdpServer("localhost", serverPort)), udpClient(UdpClient()), privateKey(CKey()){
    std::cout << "CPbft constructor" << std::endl;
    localView = 0;
    globalView = 0;
    nGroups = 1;
    privateKey.MakeNewKey(false);
    publicKey = privateKey.GetPubKey();
    pRecvBuf = new char[CPbftMessage::messageSizeBytes];
}


CPbft::~CPbft(){
    delete []pRecvBuf;
}

//------------------ func run in udp server thread-------------

void interruptableReceive(CPbft& pbftObj){
    bool fShutdown = ShutdownRequested();
    while(!fShutdown){
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbftObj.udpServer.timed_recv(pbftObj.pRecvBuf, CPbftMessage::messageSizeBytes, 500);
	// recvBytes should be greater than 5 to fill all fields of a PbftMessage object.
	if( recvBytes > 5){
	    std::cout << pbftObj.pRecvBuf[0] - '0' << std::endl;
	    // first byte is message type.
            CPre_prepare dummyPreprepare;
	    switch(static_cast<PbftPhase>(pbftObj.pRecvBuf[0] - '0')){
		case pre_prepare:
		    pbftObj.onReceivePrePrepare(dummyPreprepare);

		    break;
		case prepare:
		    std::cout << "prepare msg" << std::endl;
		    break;
		case commit:
		    std::cout << "commit msg" << std::endl;
		    break;
		case reply:
		    std::cout << "reply msg" << std::endl;
		    break;
		default:
		    std::cout << "invalid msg" << std::endl;
		    
	    }
	} 
        fShutdown = ShutdownRequested();
    }
}


void CPbft::start(){
    receiver = std::thread(interruptableReceive, std::ref(*this)); 
	receiver.join();
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
    
    nFaulty = (members.size() - 1)/3;
}


CPre_prepare CPbft::assemblePre_prepare(const CBlock& block){
    return CPre_prepare();
}

CPrepare CPbft::assemblePrepare(const uint256& digest){
    return CPrepare();
}

CCommit CPbft::assembleCommit(const uint256& digest){
    return CCommit();
}


bool CPbft::onReceivePrePrepare(const CPre_prepare& pre_prepare){
    
    std::cout<< "received pre-prepare" << std::endl;
    // verify signature and return wrong if big is wrong
    
    // add to log
//        log[pre_prepare.seq] = CPbftLogEntry(pre_prepare);
        sendPrepare();
    return true;
}

bool CPbft::onReceivePrepare(const CPrepare& prepare){
    std::cout << "received prepare. Phase  = " << log[prepare.seq].phase << std::endl;
    //verify sig. if wrong, return false.
    
    
    //add to log
    log[prepare.seq].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareArray.size() >= (nFaulty << 1) ){
	
	// enter commit phase
	std::cout << "enter commit phase" << std::endl;
	log[prepare.seq].phase = PbftPhase::commit;
	sendCommit();
	return true;
    }
    return true;
}

bool CPbft::onReceiveCommit(const CCommit& commit){
    std::cout << "received commit" << std::endl;
    //verify sig. if wrong, return false.
    
    //add to log
    log[commit.seq].commitArray.push_back(commit);
    // count the number of prepare msg. enter reply if greater than 2f+1
    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitArray.size() >= (nFaulty << 1 ) + 1 ){
	
	// enter commit phase
	std::cout << "enter reply phase" << std::endl;
	log[commit.seq].phase = PbftPhase::reply;
	excuteTransactions(commit.digest);
	return true;
    }
    return true;
}

void CPbft::sendPrepare(){
    // send prepare to all nodes in the members array.
    
    std::cout << "sending Prepare..." << std::endl; 
    CPbftMessage dummyMsg(PbftPhase::commit);
    std::ostringstream oss;
    int pbftPeerPort = std::stoi(gArgs.GetArg("-pbftpeerport", "18340"));
    dummyMsg.serialize<std::ostringstream>(oss); 
    udpClient.sendto(oss, "127.0.0.1", pbftPeerPort);
}

void CPbft::sendCommit(){
    std::cout << "sending Commit..." << std::endl;
}

void CPbft::excuteTransactions(const uint256& digest){
    std::cout << "executing tx..." << std::endl;
}

