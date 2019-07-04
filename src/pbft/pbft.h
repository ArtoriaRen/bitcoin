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
#include "netaddress.h"
#include "util.h"
#include "primitives/block.h"
#include "chain.h"
#include "net.h"
#include "pbft/udp_server_client.h"
#include "key.h"
#include "pubkey.h"

//global view number

enum PbftPhase {pre_prepare, prepare, commit, reply};

class CPbftMessage {
public:
    PbftPhase phase;
    uint32_t view;
    uint32_t seq;
    uint32_t senderId;
    uint256 digest;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    const static uint32_t messageSizeBytes = 128;
};

class CPre_prepare : public CPbftMessage{
    CBlock block; // we can use P2P network to disseminate the block before the primary send Pre_prepare msg so that the block does not have to be in the Pre-prepare message.
    
public:
    CPre_prepare(){
    	phase = PbftPhase::pre_prepare;
    }
};


class CPrepare: public CPbftMessage{

public:
    CPrepare(){
    	phase = PbftPhase::prepare;
    }
};

class CCommit: public CPbftMessage{
    
public:
    CCommit(){
    	phase = PbftPhase::commit;
    }
    
};

class CPbftLogEntry{
public:
    CPre_prepare pre_prepare;
    std::vector<CPrepare> prepareArray;
    std::vector<CCommit> commitArray;
    PbftPhase phase;
    

    CPbftLogEntry(){}
    
    CPbftLogEntry(const CPre_prepare& pp){
	pre_prepare = pp;
	// log for a pre-prepare message will not be created if the sig is wrong, so the protocol for this entry directly enter Prepare phase once created.
	phase = PbftPhase::prepare;
    }
    
};


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
    std::mutex mtx;
    std::condition_variable ready;
    std::deque<CPbftMessage> receiveQue;

    
    explicit CPbft(int serverPort, int clientPort);    
    ~CPbft();

    // There are two  threads: 1. receive udp packets 2. process packet according to the protocol (the current thread). 
    void start();

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

    friend void loopReceive(CPbft& pbftObj);

private:
    UdpServer udpServer;
    UdpClient udpClient;
    char* pRecvBuf;
    // private ECDSA key used to sign messages
    CKey privateKey;
    CPubKey publicKey; // public key should be put on the blockchain so every can verify group members.

};


#endif /* PBFT_H */

