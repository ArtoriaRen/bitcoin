/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */


#include "pbft/pbft_msg.h"
#include "hash.h"
#include "pbft.h"
//#include "pbft/debug_flag_pbft.h"

CPbftMessage::CPbftMessage(): view(0), seq(0), digest(), sigSize(0), vchSig(){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CPbftMessage::CPbftMessage(const CPbftMessage& msg): view(msg.view), seq(msg.seq), digest(msg.digest), sigSize(0), vchSig(msg.vchSig){
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CPbftMessage::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&view, sizeof(view))
	    .Write((const unsigned char*)&seq, sizeof(seq))
	    .Write((const unsigned char*)digest.begin(), digest.size())
	    .Finalize((unsigned char*)&result);
}

CPre_prepare::CPre_prepare(const CPre_prepare& msg): CPbftMessage(msg), tx(msg.tx) { }

CPre_prepare::CPre_prepare(const CPbftMessage& msg): CPbftMessage(msg) { }

CReply::CReply(): reply(), digest(), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CReply::CReply(char replyIn, const uint256& digestIn): reply(replyIn), digest(digestIn), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CReply::getHash(uint256& result){
    CHash256().Write((const unsigned char*)&reply, sizeof(reply))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}
