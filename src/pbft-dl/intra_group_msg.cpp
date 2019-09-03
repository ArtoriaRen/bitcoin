/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft-dl/intra_group_msg.h"
#include "hash.h"

CIntraGroupMsg::CIntraGroupMsg():phase(DL_pre_prepare), localView(0), globalView(0), seq(0), senderId(0), digest(), vchSig(){
}

CIntraGroupMsg::CIntraGroupMsg(uint32_t senderId):phase(DL_pre_prepare), localView(0), globalView(0), seq(0), senderId(senderId), digest(), vchSig(){
}

CIntraGroupMsg::CIntraGroupMsg(DL_Phase p, uint32_t senderId):phase(p), localView(0), globalView(0), seq(0), senderId(senderId), digest(), vchSig(){
}

CIntraGroupMsg::CIntraGroupMsg(CLocalPP& pre_prepare, uint32_t senderId):phase(pre_prepare.phase), localView(pre_prepare.localView), globalView(pre_prepare.globalView), seq(pre_prepare.seq), senderId(senderId), digest(pre_prepare.digest), vchSig(pre_prepare.vchSig){
}

void CIntraGroupMsg::serialize(std::ostringstream& s, const char* clientReq) const {
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

void CIntraGroupMsg::deserialize(std::istringstream& s, char* clientReq) {
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
    for(unsigned int i = 0; i < sigSize; i++) { 
	c = s.get();
//	std::cout  << "sig char = " << c << std::endl;
	vchSig.push_back(static_cast<unsigned char>(c));
    }
    
//    std::cout << "deserialize ends, phase = " << phase  << ", view = " << view << ", seq = " << seq << ", senderId = "<< senderId << ", digest = " << digest.GetHex() << ", sig[0] = " << vchSig[0] << std::endl;
    
}



void CIntraGroupMsg::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&phase, sizeof(phase))
	    .Write((const unsigned char*)&localView, sizeof(localView))
	    .Write((const unsigned char*)&globalView, sizeof(globalView))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)&senderId, sizeof(senderId))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}

CLocalPP::CLocalPP(const CIntraGroupMsg& msg){
    phase = DL_pre_prepare;
    localView = msg.localView;
    globalView = msg.globalView;
    seq = msg.seq;
    senderId = msg.senderId;
    digest = msg.digest;
    vchSig = msg.vchSig;
}

void CLocalPP::serialize(std::ostringstream& s) const{
    std::cout << "LocalPP serialize, add clientReq: " << clientReq << std::endl;
    // we must serialize clientReq before vchSig because we are not sure the size of vchSig and it can contain space.
    CIntraGroupMsg::serialize(s, clientReq.c_str());
}

void CLocalPP::deserialize(std::istringstream& s){
    char reqCharArr[128] = {'\0'};
    CIntraGroupMsg::deserialize(s, reqCharArr);
    clientReq = std::string(reqCharArr);
    std::cout << "client req : " << clientReq << std::endl;
}