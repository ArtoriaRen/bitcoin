/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include <iostream>
#include <sstream>
#include "pbft/util.h"
#include "pbft/pbft.h"

uint32_t deserializePublicKeyReq(char* buf, size_t recvBytes){
    // extract the sender id
    uint32_t sender_id  = 0;
    std::string recvString(buf + 2, recvBytes - 2);
    std::istringstream iss(recvString);
//    iss.get(); // discard the header char
//    iss.get(); // discard the space following header char
    iss >> sender_id; 
    return sender_id;
}


void serializePubKeyMsg(std::ostringstream& oss, const uint32_t senderId, const int port, const CPubKey& pk){
    oss << CPbft::pubKeyMsgHeader;
    oss << " ";
    oss << senderId;
    oss << " ";
    // TODO: when running on diff machines, must also add ip.
    oss << port;
    oss << " ";
    pk.Serialize(oss); 
}

// bug: peer use different ports to send and recv udp packets. The src_addr cannot be used as peer receiving port.
uint32_t deSerializePubKeyMsg(std::unordered_map<uint32_t, CPbftPeer>& map, char* pRecvBuf, const ssize_t recvBytes, const struct sockaddr_in& src_addr){
    std::string recvString(pRecvBuf, recvBytes);
    std::istringstream iss(recvString); //construct a stream start from index 2, because the first two chars ('a' and ' ') are not part of  a public key. 
    char msgHeader;
    uint32_t senderId; 
    int port;
    CPubKey pk;
    iss >> msgHeader;
    iss >> senderId;
    iss >> port;
    iss.get();
    pk.Unserialize(iss);
    std::cout << "received publicKey = (" << senderId << ", " << port << ", " << pk.GetHash().ToString() << ")"<<std::endl;
    // extract peer ip and port
    std::string ip(inet_ntoa(src_addr.sin_addr));
    CPbftPeer peer(ip, port, pk);
    map.insert(std::make_pair(senderId, peer));
    return senderId;
}
