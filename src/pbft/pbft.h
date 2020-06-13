/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft.h
 * Author: l27ren
 *
 * Created on June 11, 2020, 11:32 AM
 */

#ifndef PBFT_H
#define PBFT_H
#include "pbft/pbft_log_entry.h"
#include "key.h"
#include "net.h"
#include "pubkey.h"
#include <unordered_map>

extern bool fIsClient; // if this node is a pbft client.

class CPbft{
public:
    static const size_t logSize = 1000;  
    size_t groupSize;
    uint32_t localView;
    // pbft log. The index is sequence number.
    std::vector<CPbftLogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    int lastExecutedIndex; 
    CPubKey myPubKey;

    CNode* leader; // pbft leader
    std::vector<CNode*> otherMembers; // members other than the leader and the node itself.
    std::unordered_map<std::string, CPubKey> pubKeyMap;

    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CNode* pfrom, CPre_prepare& pre_prepare);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CPbftMessage& prepare, bool sanityCheck);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CPbftMessage& commit, bool sanityCheck);

    CPre_prepare assemblePPMsg(const CTransaction& tx);
    bool checkMsg(CNode* pfrom, CPbftMessage* msg);

private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};


class CPbftClient{
public:
    uint nTxTotal; // total number of tx to send
    uint nTxSent; // number of tx already sent
    CPbftClient();
};

extern std::unique_ptr<CPbft> g_pbft;
extern std::unique_ptr<CPbftClient> g_pbft_client;
#endif /* PBFT_H */

