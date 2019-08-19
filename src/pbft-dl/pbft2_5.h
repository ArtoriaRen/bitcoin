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
    std::unordered_map<uint32_t, std::list<CPbftMessage>> commitList;
    DL_pbft dlHandler;



    CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader);

    // send commit message only to local leader rather than broadcast to each peer.
    bool onReceivePrepare(CPbftMessage& prepare, bool sanityCheck) override;

    // only the local leader can receive local commit messages, and it store all commits in a list.
    bool onReceiveCommit(CPbftMessage& commit, bool sanityCheck) override;

    /* Once enough local commits are collected, send the commit message list to other group leaders. 
       This function will only be called by a local leader, because only local leaders can receive commits.*/
    void executeTransaction(const int seq) override;

    
    void send2Peer(uint32_t peerId, CPbftMessage* msg);
};

#endif /* PBFT2_5_H */

