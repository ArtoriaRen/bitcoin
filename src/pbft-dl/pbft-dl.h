/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft-dl.h
 * Author: l27ren
 *
 * Created on August 16, 2019, 11:19 AM
 */

#ifndef PBFT_DL_H
#define PBFT_DL_H

#include "pbft/pbft.h"
#include "pbft-dl/dl_msg.h"


class DL_pbft{
public:
    std::unordered_map<uint32_t, CPbftPeer> peerGroupLeaders; // leaders of other groups.
    uint32_t globalLeader; // peer id of global leader
    std::list<std::list<CPbftMessage>> globalPC; // global prepared cert (local commits from 2f+1 groups) 
    std::list<std::list<DL_Message>> globalCC; 

    
    // TODO: maybe need to move to upper layer class.
    void deserializeMultiCommits(std::istringstream iss);


    
    
    
};


#endif /* PBFT_DL_H */

