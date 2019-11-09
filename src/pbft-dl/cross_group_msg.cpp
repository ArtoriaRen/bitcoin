/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include "pbft-dl/debug_flags.h"
#include "pbft-dl/cross_group_msg.h"

CCrossGroupMsg::CCrossGroupMsg(): phase(DL_pre_prepare){
}

CCrossGroupMsg::CCrossGroupMsg(DL_Phase p): phase(p){
} 

CCrossGroupMsg::CCrossGroupMsg(DL_Phase p, std::vector<CIntraGroupMsg>& commits): phase(p), localCC(commits){
}

// this constructor is used to assemble a GPP message b/c client req is piggybacked.
CCrossGroupMsg::CCrossGroupMsg(DL_Phase p, std::vector<CIntraGroupMsg>& commits, std::string& req): phase(p), localCC(commits), clientReq(req){
}


void CCrossGroupMsg::serialize(std::ostringstream& s) const{
#ifdef CROSS_GROUP_DEBUG
    std::cout << "LocalCC serialize starts, phase = " << phase << std::endl; 
#endif
    s << static_cast<int> (phase);
    s << " ";
    if(phase == DL_GPP){
#ifdef CROSS_GROUP_DEBUG
	std::cout  << "GPP msg, serializing clientReq " << clientReq <<  std::endl;
#endif
	    s << clientReq; 
	    s << " ";
    }
    s << localCC.size();
    s << " ";
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
    // TODO: add number of commits in the localCC so that the temination condition is more accurate.                  
    size_t numCommits = 0;
    s >> numCommits; 
    for (int i = 0; i < numCommits; i++){
	CIntraGroupMsg cMsg(DL_commit, 0);
	s >> cMsg.phase;
#ifdef CROSS_GROUP_DEBUG
	std::cout << "phase of msg in localCC = " << cMsg.phase << std::endl;
#endif
	cMsg.deserialize(s);
	localCC.push_back(cMsg);
    }
#ifdef CROSS_GROUP_DEBUG
    std::cout << "lcoalCC deserialize finished" << std::endl;
#endif
}

CGlobalReply::CGlobalReply(): phase(DL_GR), localReplyArray(){
}

CGlobalReply::CGlobalReply(std::vector<CLocalReply>& replies): phase(DL_GR), localReplyArray(replies){
}

void CGlobalReply::serialize(std::ostringstream& s) const{
#ifdef CROSS_GROUP_DEBUG
    std::cout << "LocalCC serialize starts, phase = " << phase << std::endl; 
#endif
    s << static_cast<int> (phase);
    s << " ";
    s << localReplyArray.size();
    s << " ";
    for(CLocalReply localReply: localReplyArray){
	localReply.serialize(s);
    }

}

void CGlobalReply::deserialize(std::istringstream& s){
    // the phase has already deserialized by the udp server in CPbft2_5, so we only deserialize local reply array.

    size_t numReplies = 0;
    s >> numReplies; 
    for (int i = 0; i < numReplies; i++){
	CLocalReply localReply;
	s >> localReply.phase;
#ifdef CROSS_GROUP_DEBUG
	std::cout << "phase of msg in localCC = " << cMsg.phase << std::endl;
#endif
	localReply.deserialize(s);
	localReplyArray.push_back(localReply);
    }
#ifdef CROSS_GROUP_DEBUG
    std::cout << "lcoalCC deserialize finished" << std::endl;
#endif
}