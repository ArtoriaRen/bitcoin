/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */


#include "pbft/pbft_msg.h"
#include "hash.h"
#include "pbft.h"
//#include "pbft/debug_flag_pbft.h"

CPbftMessage::CPbftMessage(): view(0), seq(0), digest(), vchSig(){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CPbftMessage::CPbftMessage(const CPbftMessage& msg): view(msg.view), seq(msg.seq), digest(msg.digest), vchSig(msg.vchSig){
}

void CPbftMessage::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&view, sizeof(view))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)digest.begin(), digest.size())
	    .Finalize((unsigned char*)&result);
}

CPre_prepare::CPre_prepare(const CPre_prepare& msg): CPbftMessage(msg), tx(msg.tx) {
}

CPre_prepare::CPre_prepare(const CPbftMessage& msg): CPbftMessage(msg) { }

CReply::CReply(): phase(PbftPhase::reply), seq(), senderId(), reply(), digest(), vchSig(){
}

CReply::CReply(uint32_t seqNum, const uint32_t sender, char rpl, const uint256& dgt, std::string ts): 
phase(PbftPhase::reply), seq(seqNum), senderId(sender), reply(rpl), timestamp(ts), digest(dgt), vchSig(){
}

void CReply::serialize(std::ostringstream& s) const {
    s << static_cast<int> (phase);
    s << " ";
    s << seq;
    s << " ";
    s << senderId;
    s << " ";
    s << reply;
    s << " ";
    s << timestamp;
    s << " ";
    digest.Serialize(s);
    s << vchSig.size();
    s << " ";
    for (uint i = 0; i < vchSig.size(); i++) {
//	std::cout  << "i = " << i << "sig char = " << vchSig[i] << std::endl;
	s << vchSig[i];
    }
}

void CReply::deserialize(std::istringstream& s) {
    s >> seq;
    s >> senderId;
    s >> reply;
    s >> timestamp;
    s.get(); // discard the delimiter after reply.
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
    
#ifdef INTRA_GROUP_DEBUG
    std::cout << "deserialize ends, phase = " << phase  << " local view = " << localView << ", global view = " << globalView << ", seq = " << seq << ", senderId = "<< senderId << ", digest = " << digest.GetHex() << ", sig[0] = " << vchSig[0] << std::endl;
#endif
}

void CReply::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&phase, sizeof(phase))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)&senderId, sizeof(senderId))
	    .Write((const unsigned char*)&reply, sizeof(reply))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}
