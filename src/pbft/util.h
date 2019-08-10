/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   util.h
 * Author: l27ren
 *
 * Created on August 9, 2019, 4:06 PM
 */

#ifndef PBFT_UTIL_H
#define PBFT_UTIL_H

#include <unordered_map>
#include "pbft/peer.h"
#include "pubkey.h"

uint32_t deserializePublicKeyReq(char* buf, size_t recvBytes);

void serializePubKeyMsg(std::ostringstream& oss, const uint32_t senderId, const int port, const CPubKey& pk);

uint32_t deSerializePubKeyMsg(std::unordered_map<uint32_t, CPbftPeer>& map, char* pRecvBuf, ssize_t recvBytes, const struct sockaddr_in& src_addr);

#endif /* PBFT_UTIL_H */

