#include <sstream> 
#include <stdint.h>
#include "pbft-dl/pbft-dl.h"

DL_pbft::DL_pbft():globalLeader(0){
}


bool DL_pbft::checkGPP(CCrossGroupMsg& gppMsg){
    // check all sig in the leader group local-CC 
    // assemble local pre-prepare.
    return true;
    
}

bool DL_pbft::checkGP(CCrossGroupMsg& msg){
    return true;
    
}

bool DL_pbft::checkGC(CCrossGroupMsg& msg){
    return true;
    
}

void DL_pbft::sendGPP2Leaders(const CCrossGroupMsg& msg, UdpClient& udpClient){
    std::ostringstream oss;
    msg.serialize(oss);
    for(auto p : peerGroupLeaders){
	std::cout << "send GPP to peer " << p.first << std::endl;
	
	udpClient.sendto(oss, p.second.ip, p.second.port);
    }
}


void DL_pbft::sendMsg2Leaders(const CCrossGroupMsg& msg){
}