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

#include "pubkey.h"
#include "pbft/pbft.h"
#include "pbft-dl/intra_group_msg.h"
#include "pbft-dl/cross_group_msg.h"
#include "pbft-dl/dl_log_entry.h"
#include "pbft-dl/cert.h"
#include "pbft/udp_server_client.h"
#include "pbft/peer.h"

/* handle communication with other groups.
 */
class DL_pbft{
public:
    static const int FAUTY_GROUPS = 1;
    // All leaders of other groups (posibbly including the global leader). key is server id. 
    std::unordered_map<uint32_t, CPbftPeer> peerGroupLeaders;
    uint32_t globalLeader; // peer id of global leader
    // public keys of all members of other groups. Used to verify localCC from other groups.
    std::unordered_map<uint32_t, CPubKey> pkMap;
    
    DL_pbft();
    
    // Check leader group Local-CC.
    bool checkGPP(CCrossGroupMsg& msg, uint32_t currentGV, const std::vector<DL_LogEntry>& log);
    
    // Check Local-CC from 2f non-leader groups.
    bool checkGP(CCrossGroupMsg& msg, uint32_t currentGV, const std::vector<DL_LogEntry>& log);
    
    // Check GPCLC from 2f + 1 groups.
    bool checkGC(CCrossGroupMsg& msg);
    
    // send GPP to other local leaders. This is only called by the global leader.
    void sendGlobalMsg2Leaders(const CCrossGroupMsg& msg, UdpClient& udpClient);
    
    // send GlobalPC or GlobalCC to all members of the same group.
    void multicastCert(const CCert& cert, UdpClient& udpClient, const std::unordered_map<uint32_t, CPbftPeer>& peers);
};


#endif /* PBFT_DL_H */

