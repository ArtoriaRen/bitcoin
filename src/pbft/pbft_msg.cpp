/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft/pbft_msg.h"

//CPbftMessage deserialize(char* recvBuf,  PbftPhase phase, size_t recvBytes){
//    CPbftMessage msg;
//    msg.phase = phase;
//    msg.view = recvBuf[1];
//    msg.seq = recvBuf[2];
//    msg.senderId = recvBuf[3];
//    // TODO: assume recvBuf is not change. Need lock if multi-threaded deserialization.
//    msg.digest = uint256(recvBuf[4]); // 32 bytes
//    msg.vchSig = std::vector<unsigned char>(recvBuf[5], recvBuf[5] + recvBytes - 4); 
//}
