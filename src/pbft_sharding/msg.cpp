/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft_sharding/msg.h"
#include "hash.h"
#include "pbft_sharding/pbft_sharding.h"
#include "pbft_sharding/debug_flag.h"
#include "serialize.h"
#include "primitives/transaction.h"

Message::Message(): voteCommit(true), phase(PbftShardingPhase::PRE_PREPARE), view(0), seq(0), senderId(0), digest(), vchSig() {
}

Message::Message(uint32_t senderId): voteCommit(true), phase(PbftShardingPhase::PRE_PREPARE), view(0), seq(0), senderId(senderId), digest(), vchSig() {
}

Message::Message(PbftShardingPhase p, uint32_t senderId, bool voteCommitIn): voteCommit(voteCommitIn), phase(p), view(0), seq(0), senderId(senderId), digest(), vchSig() {
}

/* given a PRE-PREPARE message, we are able to assemble a PREPARE message or COMMIT message. 
 * The phase of this message will be re-assigned the caller.
 */
Message::Message(PrePrepareMsg& pre_prepare, uint32_t senderId):voteCommit(pre_prepare.voteCommit), phase(PbftShardingPhase::PREPARE), view(pre_prepare.view), seq(pre_prepare.seq), senderId(senderId), digest(pre_prepare.digest), vchSig(pre_prepare.vchSig) {
}

void Message::serialize(std::ostringstream& s, CTransactionRef clientReq) const {
#ifdef SERIALIZATION
    std::cout << "serialize start, phase = " << phase  << ", view = " << view << ", seq = " << seq << ", senderId = "<< senderId << ", digest = " << digest.GetHex() << ", sig[0] = " << vchSig[0] << std::endl;
#endif
    s << static_cast<int> (phase);
    s << " ";
    s << view;
    s << " ";
    s << seq;
    s << " ";
    s << senderId;
    s << " ";
    s << voteCommit;
    s << " ";
    if(clientReq != nullptr){
#ifdef SERIALIZATION
	std::cout  << "serializing clientReq " << clientReq <<  std::endl;
#endif
	CDataStream ds(SER_NETWORK, SERIALIZE_TRANSACTION_NO_WITNESS);
	ds << clientReq;
	    s << ds.str(); 
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

void Message::deserialize(std::istringstream& s, CTransactionRef clientReq) {
    s >> view;
    s >> seq;
    s >> senderId;
    s >> voteCommit;
    if(clientReq != nullptr){
	/* extract req string and convert it to a CDataStream, which is used to unserialize 
	 * transactions.
	 */
	std::string reqStr;
	s >> reqStr;
	CDataStream ds(&reqStr.at(0), &reqStr.at(reqStr.length() - 1), SER_NETWORK, SERIALIZE_TRANSACTION_NO_WITNESS);
	ds >> clientReq;
#ifdef SERIALIZATION
	std::cout  << "deserializing clientReq " << clientReq << std::endl;
#endif
    }
    s.get(); // discard the delimiter after senderId.
    digest.Unserialize(s); // 256 bits = 32 bytes
    size_t sigSize;
    s >> sigSize; 
    s.get(); // discard the delimiter after sigSize.
    char c;
    vchSig.clear();
    for(uint i = 0; i < sigSize; i++) { 
	c = s.get();
//	std::cout  << "sig char = " << c << std::endl;
	vchSig.push_back(static_cast<unsigned char>(c));
    }
    
//    std::cout << "deserialize ends, phase = " << phase  << ", view = " << view << ", seq = " << seq << ", senderId = "<< senderId << ", digest = " << digest.GetHex() << ", sig[0] = " << vchSig[0] << std::endl;
    
}



void Message::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&phase, sizeof(phase))
	    .Write((const unsigned char*)&view, sizeof(view))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)&senderId, sizeof(senderId))
	    .Write((const unsigned char*)&voteCommit, sizeof(voteCommit))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}

PrePrepareMsg::PrePrepareMsg(const Message& msg){
    phase = PbftShardingPhase::PRE_PREPARE;
    view = msg.view;
    seq = msg.seq;
    senderId = msg.senderId;
    digest = msg.digest;
    vchSig = msg.vchSig;
    this->setVoteCommit(msg.getVoteCommit());
}

void PrePrepareMsg::serialize(std::ostringstream& s) const{
#ifdef SERIALIZATION
    std::cout << "pre_prepare serialize, add clientReq: " << clientReq << std::endl;
#endif
    // we must serialize clientReq before vchSig because we are not sure the size of vchSig and it can contain space.
    Message::serialize(s, clientReq);
}

void PrePrepareMsg::deserialize(std::istringstream& s){
    Message::deserialize(s, clientReq);
#ifdef SERIALIZATION
    std::cout << "client req : " << clientReq << std::endl;
#endif
}

const CTransaction& PrePrepareMsg::getTx(){
    return *clientReq;
}

bool Message::getVoteCommit() const{
    return voteCommit;
}

void Message::setVoteCommit(bool voteCommitIn){
    voteCommit = voteCommitIn; 
}

Reply::Reply(): phase(PbftShardingPhase::REPLY), seq(), senderId(), reply(true), digest(), vchSig(){
}

Reply::Reply(uint32_t seqNum, const uint32_t sender, bool rpl, const uint256& dgt): 
phase(PbftShardingPhase::REPLY), seq(seqNum), senderId(sender), reply(rpl), digest(dgt), vchSig(){
}

void Reply::serialize(std::ostringstream& s) const {
    s << static_cast<int> (phase);
    s << " ";
    s << seq;
    s << " ";
    s << senderId;
    s << " ";
    s << reply;
    s << " ";
    digest.Serialize(s);
    s << vchSig.size();
    s << " ";
    for (uint i = 0; i < vchSig.size(); i++) {
//	std::cout  << "i = " << i << "sig char = " << vchSig[i] << std::endl;
	s << vchSig[i];
    }
}

void Reply::deserialize(std::istringstream& s) {
    s >> seq;
    s >> senderId;
    s >> reply;
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

void Reply::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&phase, sizeof(phase))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)&senderId, sizeof(senderId))
	    .Write((const unsigned char*)&reply, sizeof(reply))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}