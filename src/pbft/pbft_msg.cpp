/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft/pbft_msg.h"
#include "hash.h"
#include "pbft.h"

CPbftMessage::CPbftMessage():phase(PbftPhase::pre_prepare), view(0), seq(0), senderId(0), digest(), vchSig(){
}

CPbftMessage::CPbftMessage(uint32_t senderId):phase(PbftPhase::pre_prepare), view(0), seq(0), senderId(senderId), digest(), vchSig(){
}

CPbftMessage::CPbftMessage(PbftPhase p, uint32_t senderId):phase(p), view(0), seq(0), senderId(senderId), digest(), vchSig(){
}

CPbftMessage::CPbftMessage(CPre_prepare& pre_prepare, uint32_t senderId):phase(pre_prepare.phase), view(pre_prepare.view), seq(pre_prepare.seq), senderId(senderId), digest(pre_prepare.digest), vchSig(pre_prepare.vchSig){
}

void CPbftMessage::serialize(std::ostringstream& s) const {
    std::cout << "serialize start, phase = " << phase  << ", view = " << view << ", seq = " << seq << ", senderId = "<< senderId << std::endl;
    s << static_cast<int> (phase);
    s << " ";
    s << view;
    s << " ";
    s << seq;
    s << " ";
    s << senderId;
    s << " ";
    digest.Serialize(s);
    for (uint i = 0; i < vchSig.size(); i++) {
	s << vchSig[i];
    }
}

void CPbftMessage::deserialize(std::istringstream& s) {
    int phaseInt = -1;
    s >> phaseInt;
    phase = static_cast<PbftPhase>(phaseInt);
    s >> view;
    s >> seq;
    s >> senderId;
    s.get(); // discard the delimiter after senderId.
    digest.Unserialize(s); // 256 bits = 32 bytes
    char c;
    vchSig.clear();
    while (s.get(c)) { // TODO: check the ret val when no more data.
	if(c == ' ')
	    break;
	vchSig.push_back(static_cast<unsigned char>(c));
    }
    
    std::cout << "deserialize ends, phase = " << phase  << ", view = " << view << ", seq = " << seq << ", senderId = "<< senderId << std::endl;
    
}



void CPbftMessage::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&phase, sizeof(phase))
	    .Write((const unsigned char*)&view, sizeof(view))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)&senderId, sizeof(senderId))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}

CPre_prepare::CPre_prepare(const CPbftMessage& msg){
    phase = PbftPhase::pre_prepare;
    view = msg.view;
    seq = msg.seq;
    senderId = msg.senderId;
    digest = msg.digest;
    vchSig = msg.vchSig;
}

void CPre_prepare::serialize(std::ostringstream& s) const{
    CPbftMessage::serialize(s);
    s << " ";
    s << clientReq; 
    std::cout << "pre_prepare serialize, add clientReq: " << clientReq << std::endl;
}

void CPre_prepare::deserialize(std::istringstream& s){
    CPbftMessage::deserialize(s);
    s >> clientReq;
    std::cout << "client req : " << clientReq << std::endl;
}