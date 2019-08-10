/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   peer.h
 * Author: l27ren
 *
 * Created on July 26, 2019, 11:41 AM
 */

#ifndef PEER_H
#define PEER_H
#include "pubkey.h"

class CPbftPeer{
public:
    std::string ip;
    int port;
    CPubKey pk;
    
    CPbftPeer();
    CPbftPeer(std::string ip, int port, CPubKey pk);
};


#endif /* PEER_H */

