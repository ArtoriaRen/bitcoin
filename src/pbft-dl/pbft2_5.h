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
#include "pbft-dl/intra_group_msg.h"
#include "pbft-dl/cross_group_msg.h"
#include <unordered_map>
#include "netaddress.h"
#include "util.h"
#include "primitives/block.h"
#include "chain.h"
#include "net.h"
#include "pbft/udp_server_client.h"
#include "key.h"
#include "pubkey.h"
#include "pbft/pbft_msg.h"
#include "pbft/pbft_log_entry.h"
#include "pbft/peer.h"
#include "pbft-dl/dl_log_entry.h"
#include "pbft-dl/cert.h"


class CPbft2_5{
public:
    uint32_t nFaulty; // number of faulty nodes in this group, i.e. f. group size should be 3 * nFaulty + 1;
    uint32_t nFaultyGroups; // total number of groups, i.e. F 
    uint32_t localLeader; // peer id of local leader
    DL_pbft dlHandler;
    
    static const size_t logSize = 2002; 
    uint32_t localView;
    uint32_t globalView;
    // pbft log. The index is sequence number.
    std::vector<DL_LogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    int lastExecutedIndex;
    // TODO: parameters should be put in a higher layer class. They are not part of pbft.
    CService leader;
    std::vector<uint32_t> members; // the sever_ids of peers who is a member of this group.
    uint32_t server_id;
    // peers member variable defined in the CPbft class must be nodes in the same group.
    // ----placeholder: send public keys over udp instead of extract it from the blockchain.
    std::unordered_map<uint32_t, CPbftPeer> peers; // number of peers should be 3 * Faulty + 1.
    
    
private:
    std::shared_ptr<UdpServer> udpServer;
    UdpClient udpClient;
    std::shared_ptr<char> pRecvBuf;
    // private ECDSA key used to sign messages
    CKey privateKey;
    CPubKey publicKey; // public key should be put on the blockchain so every can verify group members.
    std::thread receiver;
    std::unordered_map<int, char> data; // emulate the in-memory key-value store. x is the only key though.
    
public:
    
    CPbft2_5();

    CPbft2_5(int serverPort, unsigned int id, uint32_t l_leader, uint32_t numFaultyGroups);
    
    CPbft2_5& operator = (const CPbft2_5& rhs);

    ~CPbft2_5();
    
    // Check Pre-prepare message signature and send Prepare message
    bool onReceivePrePrepare(CLocalPP& pre_prepare) ;
    
    // send commit message only to local leader rather than broadcast to each peer.
    bool onReceivePrepare(CIntraGroupMsg& prepare, bool sanityCheck) ;
    
    // only the local leader can receive local commit messages, and it store all commits in a list.
    bool onReceiveCommit(CIntraGroupMsg& commit, bool sanityCheck) ;
    
    // only the local leader can receive GPP. GPP = {phase=GPP, localCC, req}
    bool onReceiveGPP(CCrossGroupMsg& gpp);
    
    // only the local leader can receive GP.
    bool onReceiveGP(CCrossGroupMsg& gp, bool sanityCheck);
    
    // a node should send a commit msg to ack receiving a global PC.
    bool onReceiveGPCD(const CCertMsg& gpcd);

    // only local leaders can receive GPLC
    bool onReceiveGPLC(CIntraGroupMsg& gplc);

    // only local leaders can receive GC
    bool onReceiveGC(CCrossGroupMsg& gc, bool sanityCheck);

    /* a node should send a local reply msg to after receiving a global CC 
     * and executing the request.
     */
    bool onReceiveGCCD(const CCertMsg& gccd);
    
    // a node should send a commit msg to ack receiving a global CC.
    bool onReceiveLR(CLocalReply& lr);

    /* Once enough local commits are collected, send the commit message list to 
     * other group leaders. This function will only be called by a local leader, 
     * because only local leaders can receive commits.
     */
    void executeTransaction(const int seq) ;
    
    /* assemble global messages */
    CCrossGroupMsg assembleGPP(uint32_t seq);
    
    CCrossGroupMsg assembleGP(uint32_t seq);
    
    CCrossGroupMsg assembleGC(uint32_t seq);

    CGlobalReply assembleGR(uint32_t seq);
    
    bool checkMsg(CIntraGroupMsg* msg);

    bool checkMsg(CLocalReply& msg);
    
    /* assemble local messages */
    CLocalPP assemblePre_prepare(uint32_t seq, std::string clientReq);
    CIntraGroupMsg assembleMsg(DL_Phase phase, uint32_t seq);
    CLocalReply assembleLocalReply(uint32_t seq);
    void broadcast(CIntraGroupMsg* msg);
    // ------placeholder: may be used to send ip.
    CPubKey getPublicKey();
    void broadcastPubKey();
    void sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id);
    void broadcastPubKeyReq();
    // start a thread to receive udp packets and process packet according to the dl_pbft protocol . 
    void start() ;
    
    friend void DL_Receive(CPbft2_5& pbft2_5Obj);
    
    void send2Peer(uint32_t peerId, CIntraGroupMsg* msg);
    void send2Peer(uint32_t peerId, CLocalReply& msg);
};

void DL_Receive(CPbft2_5& pbft2_5Obj);

#endif /* PBFT2_5_H */

