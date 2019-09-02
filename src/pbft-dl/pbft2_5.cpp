#include "pbft-dl/pbft2_5.h"
#include "pbft-dl/pbft-dl.h"
#include "init.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "pbft/peer.h"
#include "pbft/util.h"

CPbft2_5::CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader):CPbft(serverPort, id){
    localLeader = l_leader;
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
	// timeout block on receving a new packet. Attention: timeout is in milliseconds. 
	ssize_t recvBytes =  pbft2_5Obj.udpServer.timed_recv(pbft2_5Obj.pRecvBuf.get(), CPbftMessage::messageSizeBytes, 500, &src_addr, &len);
	
	if(recvBytes == -1){
	    // timeout. but we have got peer publickey. do nothing.
	    continue;
	}
	
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
		CPre_prepare pp = pbft2_5Obj.assemblePre_prepare(seq, clientReq);
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
		CPre_prepare ppMsg(pbft2_5Obj.server_id);
		std::cout << "recvBytes = " << recvBytes << std::endl;
		ppMsg.deserialize(iss);
		pbft2_5Obj.onReceivePrePrepare(ppMsg);
		break;
	    }
	    case static_cast<int>(prepare):
	    {
		CPbftMessage pMsg(PbftPhase::prepare, pbft2_5Obj.server_id);
		pMsg.deserialize(iss);
		pbft2_5Obj.onReceivePrepare(pMsg, true);
		break;
	    } 
	    case static_cast<int>(commit):
	    {
		CPbftMessage cMsg(PbftPhase::commit, pbft2_5Obj.server_id);
		cMsg.deserialize(iss);
		pbft2_5Obj.onReceiveCommit(cMsg, true);
		break;
	    }
	    case static_cast<int>(DLPP_GPP):
	    {
		DL_Message gppMsg(DL_Phase::DLPP_GPP, pbft2_5Obj.server_id);
		gppMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGPP(gppMsg);
	    }
	    case static_cast<int>(DLP_GP):
	    {
		DL_Message gpMsg(DL_Phase::DLP_GP, pbft2_5Obj.server_id);
		gpMsg.deserialize(iss);
		pbft2_5Obj.onReceiveGP(gpMsg);
	    }
	    //	    case DLP_GPCD:
	    //	    {
	    //		DL_Message gpcdMsg(DL_Phase::DLP_GPCD, pbft2_5Obj.server_id);
	    //		gpcdMsg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGPCD(gpMsg);
	    //	    }
	    //	    case DLC_GPLC:
	    //	    {
	    //		DL_Message gpcdMsg(DL_Phase::DLC_GPLC, pbft2_5Obj.server_id);
	    //		gpcdMsg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGPLC(gpMsg);
	    //	    }
	    //	    case DLC_GC:
	    //	    {
	    //		DL_Message msg(DL_Phase::DLC_GC, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGC(msg);
	    //	    }
	    //	    case DLC_GCCD:
	    //	    {
	    //		DL_Message msg(DL_Phase::DLC_GCCD, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveGCCD(msg);
	    //	    }
	    //	    case DLR_LR:
	    //	    {
	    //		DL_Message msg(DL_Phase::DLR_LR, pbft2_5Obj.server_id);
	    //		msg.deserialize(iss);
	    //		pbft2_5Obj.dlHandler.onReceiveLR(msg);
	    //	    }
	    //	    case DLR_GR:
	    //	    {
	    //		DL_Message msg(DL_Phase::DLR_GR, pbft2_5Obj.server_id);
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

bool CPbft2_5::onReceivePrePrepare(CPre_prepare& pre_prepare){
    return true;

}

bool CPbft2_5::onReceivePrepare(CPbftMessage& prepare, bool sanityCheck){
    std::cout << "2_5 received prepare." << std::endl;
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&prepare)){
	return false;
    }
    
    // count the number of prepare msg. enter commit if greater than 2f
    log[prepare.seq].prepareCount++;
    //use == (nFaulty << 1) instead of >= (nFaulty << 1) so that we do not re-send commit msg every time another prepare msg is received.  
    if(log[prepare.seq].phase == PbftPhase::prepare && log[prepare.seq].prepareCount == (nFaulty << 1)){
	// enter commit phase
	std::cout << "server " << server_id << " enter commit phase" << std::endl;
	log[prepare.seq].phase = PbftPhase::commit;
	CPbftMessage c = assembleMsg(PbftPhase::commit, prepare.seq); 
	if(server_id != localLeader){
	    // send commit only to local leader
	    send2Peer(localLeader, &c);
	} else {
	    log[prepare.seq].commitCount++;
	}
	
	return true;
    }
    return true;
}

bool CPbft2_5::onReceiveCommit(CPbftMessage& commit, bool sanityCheck){
    std::cout << "received commit" << std::endl;
    // sanity check for signature, seq, view.
    if(sanityCheck && !checkMsg(&commit)){
	return false;
    }
    
    // count the number of prepare msg. enter execute if greater than 2f+1
    log[commit.seq].commitCount++;
    commitList[commit.seq].push_back(commit);
    if(log[commit.seq].phase == PbftPhase::commit && log[commit.seq].commitCount == (nFaulty << 1 ) + 1 ){ 
	if(server_id == dlHandler.globalLeader){
	    std::cout << "server " << server_id << "multicast GPP " << log[commit.seq].pre_prepare.clientReq << std::endl;
	    DL_Message gpp = assembleGPP();
	    dlHandler.sendMsg2Leaders(gpp);
	} else {
	    std::cout << "server " << server_id << "multicast GP " << log[commit.seq].pre_prepare.clientReq << std::endl;
	    DL_Message gp = assembleGP();
	    dlHandler.sendMsg2Leaders(gp);
	}
	//	executeTransaction(commit.seq);
	return true;
    }
    return true;
}

bool CPbft2_5::onReceiveGPP(DL_Message& commit){
    return true;
}

bool CPbft2_5::onReceiveGP(DL_Message& commit){
    return true;
}

void CPbft2_5::executeTransaction(const int seq){
    std::ostringstream oss;
    // serialize all commit messages into one stream.
    for(auto c : commitList.at(seq)){
	c.serialize(oss); 
    }
    
    // send the stream to all group leader
    for(auto it = dlHandler.peerGroupLeaders.begin(); it != dlHandler.peerGroupLeaders.end(); it++){
	udpClient.sendto(oss, it->second.ip, it->second.port);
	
    }
    
}


DL_Message CPbft2_5::assembleGPP(){
    return DL_Message();
}

DL_Message CPbft2_5::assembleGP(){
    return DL_Message();
}


void CPbft2_5::send2Peer(uint32_t peerId, CPbftMessage* msg){
    std::ostringstream oss;
    msg->serialize(oss); 
    udpClient.sendto(oss, peers[peerId].ip, peers[peerId].port);
    
}