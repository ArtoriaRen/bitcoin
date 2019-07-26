/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbft/peer.h"

CPbftPeer::CPbftPeer(): pk(CPubKey()){
}


CPbftPeer::CPbftPeer(std::string ip, int port, CPubKey pk){
    this->ip = ip;
    this->port = port;
    this->pk = pk;
}