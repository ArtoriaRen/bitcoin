/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft2_5.h
 * Author: l27ren
 *
 * Created on August 16, 2019, 12:43 PM
 */

#ifndef PBFT2_5_H
#define PBFT2_5_H
#include "pbft/pbft.h"
#include "pbft-dl/pbft-dl.h"


class CPbft2_5 : public CPbft{
public:
    uint32_t localLeader; // peer id of local leader
    // peers member variable defined in the CPbft class must be nodes in the same group.
    // store the commit messages from group members for every seq. (key is seq) 
    std::unordered_map<uint32_t, std::list<CPbftMessage>> commitList; 
    DL_pbft dlHandler;



    CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader);

    ~CPbft2_5();

    // Check Pre-prepare message signature and send Prepare message
    bool onReceivePrePrepare(CPre_prepare& pre_prepare) override;

    // send commit message only to local leader rather than broadcast to each peer.
    bool onReceivePrepare(CPbftMessage& prepare, bool sanityCheck) override;

    // only the local leader can receive local commit messages, and it store all commits in a list.
    bool onReceiveCommit(CPbftMessage& commit, bool sanityCheck) override;

    // only the local leader can receive GPP.
    bool onReceiveGPP(DL_Message& commit);

    // only the local leader can receive GP.
    bool onReceiveGP(DL_Message& commit);

    /* Once enough local commits are collected, send the commit message list to other group leaders. 
       This function will only be called by a local leader, because only local leaders can receive commits.*/
    void executeTransaction(const int seq) override;

    DL_Message assembleGPP();

    DL_Message assembleGP();

    // start a thread to receive udp packets and process packet according to the dl_pbft protocol . 
    void start() override;

    friend void DL_Receive(CPbft2_5& pbft2_5Obj);
    
    void send2Peer(uint32_t peerId, CPbftMessage* msg);
};

#endif /* PBFT2_5_H */

