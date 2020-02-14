/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft_multi.h
 * Author: liuyangren
 *
 * Created on Feb 9, 2020, 4:37 PM
 */


#ifndef PBFT_SHARDING_H
#define PBFT_SHARDING_H
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
#include "msg.h"
#include "log_entry.h"
#include "pbft/peer.h"


class CPbftSharding{
public:
    /* limit log size to 2002 because YCSB workloads load and run add up to 
     * 2000 operations and our basic test cases in pbft_test.cpp send 2 requests.
     */
    static const size_t logSize = 2002;  
    size_t groupSize;
    uint32_t localView;
    //uint32_t globalView;
    // pbft log. The index is sequence number.
    std::vector<CLogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    int lastExecutedIndex;
    
    // TODO: parameters should be put in a higher layer class. They are not part of pbft.
    CService leader;
    std::vector<uint32_t> members; // the sever_ids of peers who is a member of this group.
    uint32_t server_id;
    uint32_t nFaulty; // number of faulty nodes in this group.
    uint32_t nGroups; // total number of groups.
    
    // udp server convert received char array into Message and put them in a queue.(May not be used if we process msg in the udp server thread.) 
    //    std::mutex mtxMsg;
    //    std::condition_variable ready;
    // std::deque<Message> receiveQue;
    
    CPbftSharding();
    explicit CPbftSharding(int serverPort, unsigned int id, size_t numNodes);    
    
    CPbftSharding& operator = (const CPbftSharding& rhs);

    ~CPbftSharding();
    // start a thread to receive udp packets and process packet according to the protocol . 
    void start();
    
    // calculate the leader and group members based on the random number and the blockchain.
    void group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindex);
    
    
    // Check Pre-prepare message signature and send Prepare message
    bool onReceivePrePrepare(PrePrepareMsg& pre_prepare);
    
    
    //TODO: find the key used to sign and verify messages 
    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool onReceivePrepare(Message& prepare, bool sanityCheck);
    
    
    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool onReceiveCommit(Message& commit, bool sanityCheck);
    
    
    bool checkMsg(Message* msg);
    PrePrepareMsg assemblePre_prepare(uint32_t seq, CDataStream& txnMsg);
    Message assembleMsg(PbftShardingPhase phase, uint32_t seq);
    Reply assembleReply(uint32_t seq);
    void broadcast(Message* msg);
    // ------placeholder: may be used to send ip.
//    void broadcastPubKey();
//    void sendPubKey(const struct sockaddr_in& src_addr, uint32_t recver_id);
//    void broadcastPubKeyReq();
    
    void executeTransaction(const int seq);
    void sendReply2Client(const int seq);
    
    friend void interruptableReceive(CPbftSharding& pbftShardingObj);
    
private:
    std::shared_ptr<UdpServer> udpServer;
    UdpClient udpClient;
    std::shared_ptr<char> pRecvBuf;
    // private ECDSA key used to sign messages
    CKey privateKey;
    CPubKey publicKey; // public key should be put on the blockchain so every can verify group members.
    std::thread receiver;
    int x; // emulate the in-memory key-value store. x is the only key though.
    std::unordered_map<int, char> data; // emulate the in-memory key-value store. x is the only key though.
public:
    CPubKey getPublicKey();
    // ----placeholder: send public keys over udp instead of extract it from the blockchain.
    std::unordered_map<uint32_t, CPbftPeer> peers;
    static const char pubKeyMsgHeader = 'a'; // use to identify public key exchange message.
    static const char pubKeyReqHeader = 'b'; // use to identify public key exchange message.
    static const char clientReqHeader = 'r'; // use to identify client request message.
};


void interruptableReceive(CPbftSharding& pbftShardingObj);

#endif /* PBFT_H */

