#include <pbft-dl/pbft-dl.h>
#include <sstream> 
#include <stdint.h>

void DL_pbft::deserializeMultiCommits(std::istringstream iss){
    std::list<CPbftMessage> groupCommits;
    while(!iss.eof()){
	CPbftMessage cMsg(PbftPhase::commit, 0);
	cMsg.deserialize(iss);
    }
    
}

bool DL_pbft::checkGPP(DL_Message& gppMsg){
    // check all sig in the leader group local-CC 
    // assemble local pre-prepare.
    return true;
    
}

bool DL_pbft::checkGP(DL_Message& msg){
    return true;
    
}

bool DL_pbft::checkGC(DL_Message& msg){
    return true;
    
}

void DL_pbft::sendMsg2Leaders(DL_Message msg){
    
}