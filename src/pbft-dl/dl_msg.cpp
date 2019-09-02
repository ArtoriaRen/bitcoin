/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft-dl/dl_msg.h"
#include "hash.h"
#include "pbft/pbft_msg.h"

DL_Message::DL_Message():phase(PbftPhase::pre_prepare), localView(0), globalView(0), seq(0), senderId(0), digest(), vchSig(){
}

DL_Message::DL_Message(uint32_t senderId):phase(PbftPhase::pre_prepare), localView(0), globalView(0), seq(0), senderId(senderId), digest(), vchSig(){
}

DL_Message::DL_Message(DL_Phase p, uint32_t senderId):phase(p), localView(0), globalView(0), seq(0), senderId(senderId), digest(), vchSig(){
}

// DL_Message::DL_Message(CPre_prepare& pre_prepare, uint32_t senderId):phase(pre_prepare.phase), view(pre_prepare.view), seq(pre_prepare.seq), senderId(senderId), digest(pre_prepare.digest), vchSig(pre_prepare.vchSig){
//}

void DL_Message::serialize(std::ostringstream& s, const char* clientReq) const {
    std::cout << "dl serialize start, phase = " << phase  << ", localView = " << localView  << ", globalView = " << globalView << ", seq = " << seq << ", senderId = "<< senderId << ", digest = " << digest.GetHex() << ", sig[0] = " << vchSig[0] << std::endl;
    s << static_cast<int> (phase);
    s << " ";
    s << localView;
    s << " ";
    s << globalView;
    s << " ";
    s << seq;
    s << " ";
    s << senderId;
    s << " ";
    if(clientReq != nullptr){
	std::cout  << "serializing clientReq " << clientReq <<  std::endl;
	    s << clientReq; 
	    s << " ";
    }
    digest.Serialize(s);
    s << vchSig.size();
    s << " ";
    for (uint i = 0; i < vchSig.size(); i++) {
//	std::cout  << "i = " << i << "sig char = " << vchSig[i] << std::endl;
	s << vchSig[i];
    }
}

void DL_Message::deserialize(std::istringstream& s, char* clientReq) {
    s >> localView;
    s >> globalView;
    s >> seq;
    s >> senderId;
    if(clientReq != nullptr){
	    s >> clientReq;
	std::cout  << "deserializing clientReq " << clientReq << std::endl;
    }
    s.get(); // discard the delimiter after senderId.
    digest.Unserialize(s); // 256 bits = 32 bytes
    size_t sigSize;
    s >> sigSize; 
    s.get(); // discard the delimiter after sigSize.
    char c;
    vchSig.clear();
    for(int i = 0; i < sigSize; i++) { 
	c = s.get();
//	std::cout  << "sig char = " << c << std::endl;
	vchSig.push_back(static_cast<unsigned char>(c));
    }
    
//    std::cout << "deserialize ends, phase = " << phase  << ", view = " << view << ", seq = " << seq << ", senderId = "<< senderId << ", digest = " << digest.GetHex() << ", sig[0] = " << vchSig[0] << std::endl;
    
}



void DL_Message::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&phase, sizeof(phase))
	    .Write((const unsigned char*)&localView, sizeof(localView))
	    .Write((const unsigned char*)&globalView, sizeof(globalView))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)&senderId, sizeof(senderId))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}