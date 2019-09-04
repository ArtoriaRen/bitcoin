/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft-dl/cross_group_msg.h"

CCrossGroupMsg::CCrossGroupMsg(): phase(DL_pre_prepare){
}

CCrossGroupMsg::CCrossGroupMsg(DL_Phase p): phase(p){
} 

CCrossGroupMsg::CCrossGroupMsg(DL_Phase p, std::vector<CIntraGroupMsg>& commits): phase(p), localCC(commits){
}

CCrossGroupMsg::CCrossGroupMsg(DL_Phase p, std::vector<CIntraGroupMsg>& commits, std::string& req): phase(p), localCC(commits), clientReq(req){
}

void CCrossGroupMsg::serialize(std::ostringstream& s) const{
    std::cout << "LocalCC serialize starts, phase = " << phase << std::endl; 
    s << static_cast<int> (phase);
    s << " ";
    if(phase == DL_GPP){
	std::cout  << "GPP msg, serializing clientReq " << clientReq <<  std::endl;
	    s << clientReq; 
	    s << " ";
    }
    for(CIntraGroupMsg commit : localCC){
	commit.serialize(s);
    }

}

void CCrossGroupMsg::deserialize(std::istringstream& s){
    // the phase has already by the udp server function, so we only deserialize client req, if applicable, and Local-CC here.
    if(phase == DL_GPP){
	s >> clientReq;
	std::cout << "client req in GPP = " << clientReq << std::endl;
    }
    while(!s.eof()){
	CIntraGroupMsg cMsg(DL_commit, 0);
	s >> cMsg.phase;
	std::cout << "phase of msg in localCC = " << cMsg.phase << std::endl;
	cMsg.deserialize(s);
	localCC.push_back(cMsg);
    }
    std::cout << "lcoalCC deserialize finished" << std::endl;
//	int test;
//	for(int i = 0; i < 7; i++){
//	s >> test;
//	std::cout << test << std::endl;
//	}
	
}