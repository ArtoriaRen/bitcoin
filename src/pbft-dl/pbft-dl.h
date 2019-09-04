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
#include "pbft/udp_server_client.h"

/* handle communication with other groups.
 */
class DL_pbft{
public:
    static const int FAUTY_GROUPS = 1;
    std::unordered_map<uint32_t, CPbftPeer> peerGroupLeaders; // leaders of other groups. key is server id.
    uint32_t globalLeader; // peer id of global leader

    DL_pbft();
    
    // Check leader group Local-CC.
    bool checkGPP(CCrossGroupMsg& msg);

    // Check Local-CC from 2f non-leader groups.
    bool checkGP(CCrossGroupMsg& msg);
    
    // Check GPCLC from 2f + 1 groups.
    bool checkGC(CCrossGroupMsg& msg);

    void sendGPP2Leaders(const CCrossGroupMsg& msg, UdpClient& udpClient);

    // send msg to other group leaders.
    void sendMsg2Leaders(const CCrossGroupMsg& msg);

};


#endif /* PBFT_DL_H */

