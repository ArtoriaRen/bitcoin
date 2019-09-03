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
#include "pbft-dl/intra_group_msg.h"
#include "pbft-dl/cross_group_msg.h"

/* handle communication with other groups.
 */
class DL_pbft{
public:
    std::unordered_map<uint32_t, CPbftPeer> peerGroupLeaders; // leaders of other groups.
    uint32_t globalLeader; // peer id of global leader
    std::list<std::list<CCrossGroupMsg>> globalPC; // global prepared cert (local commits from 2f+1 groups) 
    std::list<std::list<CCrossGroupMsg>> globalCC; 

    //TODO: add constructor, otherwise, globalLeader seems like not being initialized.
    
    // TODO: maybe need to move to upper layer class.
    void deserializeMultiCommits(std::istringstream iss);

    // Check leader group Local-CC.
    bool checkGPP(CCrossGroupMsg& msg);

    // Check Local-CC from 2f non-leader groups.
    bool checkGP(CCrossGroupMsg& msg);
    
    // Check GPCLC from 2f + 1 groups.
    bool checkGC(CCrossGroupMsg& msg);

    // send msg to other group leaders.
    void sendMsg2Leaders(CCrossGroupMsg msg);

};


#endif /* PBFT_DL_H */

