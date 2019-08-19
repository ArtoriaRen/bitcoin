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