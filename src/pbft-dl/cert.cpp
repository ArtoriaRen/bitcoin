/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include "pbft-dl/cert.h"

CCertMsg::CCertMsg(): phase(DL_GPCD), certSize(3){ // 3 is the minimum size of a cert under nFaultyGroups = 1.
}

CCertMsg::CCertMsg(DL_Phase p, uint32_t size): phase(p), certSize(size){
}

CCertMsg::CCertMsg(DL_Phase p, uint32_t size, std::deque<CCrossGroupMsg>& cert): phase(p), certSize(size), globalCert(cert){
}

void CCertMsg::serialize(std::ostringstream& s) const {
    s << static_cast<int> (phase);
    s << " ";
    for(auto localCC : globalCert){
	localCC.serialize(s);
    }
}

void CCertMsg::deserialize(std::istringstream& s){
   for(unsigned int i = 0; i < certSize; i++ ){
	CCrossGroupMsg crossMsg;
	int phaseNum = -1;
	s >> phaseNum;
	crossMsg.phase = static_cast<DL_Phase>(phaseNum);
#ifdef CROSS_GROUP_DEBUG
	std::cout << "phase of msg in CrossGroupMsg = " << crossMsg.phase << std::endl;
#endif
	crossMsg.deserialize(s);
	globalCert.push_back(crossMsg);
    }
#ifdef CROSS_GROUP_DEBUG
    std::cout << "cert deserialize finished" << std::endl;
#endif
}