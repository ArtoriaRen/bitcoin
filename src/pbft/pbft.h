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
#include "pbft/peer.h"


class CPbft{
public:
    static const size_t logSize = 128; 
    static const size_t groupSize = 4;
    uint32_t localView;
    uint32_t globalView;
    // pbft log. The index is sequence number.
    std::vector<CPbftLogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    
    // TODO: parameters should be put in a higher layer class. They are not part of pbft.
    CService leader;
    std::vector<uint32_t> members; // the sever_ids of peers who is a member of this group.
    uint32_t server_id;
    uint32_t nFaulty; // number of faulty nodes in this group.
    uint32_t nGroups; // total number of groups.
    
    // udp server convert received char array into CPbftMessage and put them in a queue.(May not be used if we process msg in the udp server thread.) 
    std::mutex mtxMsg;
    std::condition_variable ready;
    std::deque<CPbftMessage> receiveQue;
    
    
    explicit CPbft(int serverPort, unsigned int id);    
    ~CPbft();
    
    // start a thread to receive udp packets and process packet according to the protocol . 
    void start();
    // Stop udp server.
    void stop();
    
    // calculate the leader and group members based on the random number and the blockchain.
    void group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindex);
    
    
    // Check Pre-prepare message signature and send Prepare message
    bool onReceivePrePrepare(CPbftMessage& pre_prepare);
    
    
    //TODO: find the key used to sign and verify messages 
    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool onReceivePrepare(CPbftMessage& prepare, bool sanityCheck);
    
    
    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool onReceiveCommit(CPbftMessage& commit, bool sanityCheck);
    
    
    bool checkMsg(CPbftMessage& msg);
    CPre_prepare assemblePre_prepare(uint32_t seq, std::string clientReq);
    CPbftMessage assembleMsg(PbftPhase phase, uint32_t seq);
    void broadcast(CPbftMessage* msg);
    // ------placeholder: may be used to send ip.
    void broadcastPubKey();
    void broadcastPubKeyReq();
    
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
    int x; // emulate the in-memory key-value store. x is the only key though.
public:
    CPubKey getPublicKey();
    // ----placeholder: send public keys over udp instead of extract it from the blockchain.
    std::unordered_map<uint32_t, CPbftPeer> peers;
    static const char pubKeyMsgHeader = 'a'; // use to identify public key exchange message.
    static const char pubKeyReqHeader = 'b'; // use to identify public key exchange message.
    static const char clientReqHeader = 'r'; // use to identify client request message.
};


void interruptableReceive(CPbft& pbftObj);

#endif /* PBFT_H */

