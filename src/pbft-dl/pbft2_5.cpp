#include "pbft/peer.h"
#include "pbft-dl/pbft2_5.h"

CPbft2_5::CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader):CPbft(serverPort, id){
    localLeader = l_leader;
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
	// enter execute phase
	std::cout << "server " << server_id << "enter execute phase, req = " << log[commit.seq].pre_prepare.clientReq << std::endl;
	log[commit.seq].phase = PbftPhase::execute;
//	executeTransaction(commit.seq);
	return true;
    }
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


void CPbft2_5::send2Peer(uint32_t peerId, CPbftMessage* msg){
    std::ostringstream oss;
    msg->serialize(oss); 
    udpClient.sendto(oss, peers[peerId].ip, peers[peerId].port);
    
}