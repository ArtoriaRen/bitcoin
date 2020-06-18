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
extern std::string leaderAddrString;
extern std::string clientAddrString;

class CPbft{
public:
    static const size_t logSize = 1000;  
    size_t groupSize;
    uint32_t nFaulty;
    uint32_t localView;
    // pbft log. The index is sequence number.
    std::vector<CPbftLogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    int lastExecutedIndex; 
    CPubKey myPubKey;

    CNode* leader; // pbft leader
    CNode* client; // pbft leader
    /* TODO: remove the leader from the otherMembers vector. */
    std::vector<CNode*> otherMembers; // members other than the leader and the node itself.
    std::unordered_map<std::string, CPubKey> pubKeyMap;

    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CNode* pfrom, CConnman* connman, CPre_prepare& ppMsg);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CNode* pfrom, CConnman* connman, CPbftMessage& pMsg, bool fCheck = true);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CNode* pfrom, CConnman* connman, CPbftMessage& cMsg, bool fCheck = true);

    CPre_prepare assemblePPMsg(const CTransaction& tx);
    CPbftMessage assembleMsg(uint32_t seq); 
    CReply assembleReply(uint32_t seq);
    bool checkMsg(CNode* pfrom, CPbftMessage* msg);
    void executeTransaction(const int seq);

private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};



extern std::unique_ptr<CPbft> g_pbft;
#endif /* PBFT_H */

