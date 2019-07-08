/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft.h
 * Author: liuyangren
 *
 * Created on June 4, 2019, 2:37 PM
 */

// TODO: serialize all pbft messages

#ifndef PBFT_H
#define PBFT_H
#include <unordered_map>
#include <mutex>
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


class CPbft{
public:
    int globalView;
    int localView;
    // pbft log. The index is sequence number.
    std::vector<CPbftLogEntry> log;
    // group member list (use hard coded ip address first)
    std::vector<CService> members;
    CService leader;
    
    // TODO: maybe these two parameters should be put in a higher layer class. They are not part of pbft.
    uint32_t nGroups; // number of groups.
    uint32_t nFaulty;
    
    // udp server convert received char array into CPbftMessage and put them in a queue. 
    std::mutex mtxMsg;
    std::condition_variable ready;
    std::deque<CPbftMessage> receiveQue;

    
    explicit CPbft(int serverPort);    
    ~CPbft();

    // There are two  threads: 1. receive udp packets 2. process packet according to the protocol (the current thread). 
    void start();
    // Stop udp server.
    void stop();

    // calculate the leader and group members based on the random number and the blockchain.
    void group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindex);
    
    CPre_prepare assemblePre_prepare(const CBlock& block);
    CPrepare assemblePrepare(const uint256& digest);
    CCommit assembleCommit(const uint256& digest);
    
    // Check Pre-prepare message signature and send Prepare message
    bool onReceivePrePrepare(const CPre_prepare& pre_prepare);
    
    void sendPrepare();
    
    //TODO: find the key used to sign and verify messages 
    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool onReceivePrepare(const CPrepare& prepare);
	
    void sendCommit();
    
    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool onReceiveCommit(const CCommit& commit);
    
    
    // TODO: may block header hash can be used as digest?
    void excuteTransactions(const uint256& digest);

    friend void interruptableReceive(CPbft& pbftObj);

private:
    UdpServer udpServer;
    UdpClient udpClient;
    char* pRecvBuf;
    // private ECDSA key used to sign messages
    CKey privateKey;
    CPubKey publicKey; // public key should be put on the blockchain so every can verify group members.
    std::thread receiver;

};


#endif /* PBFT_H */

