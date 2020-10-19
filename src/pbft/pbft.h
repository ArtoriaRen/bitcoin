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
#include "pbft/pbft_msg.h"
#include "key.h"
#include "net.h"
#include "pubkey.h"
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>

extern int32_t pbftID;
extern int32_t nMaxReqInFly; 
extern int32_t QSizePrintPeriod;
extern int32_t maxBlockSize; 
extern bool testStarted;

class ThreadSafeQueue {
public:
    ThreadSafeQueue();
    ~ThreadSafeQueue();

    CMutableTxRef& front();
    std::deque<CMutableTxRef> get_all();
    std::deque<CMutableTxRef> get_upto(uint32_t upto);
    void pop_front();

    void push_back(const CMutableTxRef& item);
    void push_back(CMutableTxRef&& item);

    int size();
    bool empty();

private:
    std::deque<CMutableTxRef> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class CPbft{
public:
    // TODO: may need to recycle log slots for throughput test. Consider deque.
    static const size_t logSize = 3000;  
    static const size_t groupSize = 4;
    static const uint32_t nFaulty = 1;
    static const int32_t clientID = 65; // the pbftID of the client.
    uint32_t localView;
    // pbft log. The index is sequence number.
    std::vector<CPbftLogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    volatile int lastExecutedSeq; 
    CPubKey myPubKey;

    CNode* client; // pbft client
    /* all peers in the network: both leaders and followers; both in our committee
     * and other committees. The index is peerID. Any peers whose peerID % groupSize
     * = 0 are leaders of committee peerID/groupSize. */
    std::vector<CNode*> peers;  
    std::unordered_map<int32_t, CPubKey> pubKeyMap;

    int nReqInFly; 
    uint32_t nCompletedTx;
    /* a queue storing client req waiting for being processed. */
    ThreadSafeQueue reqQueue;
    /* we need the client conn man to wake up the client listening thread to send
     * reply back the client as soon as possible. */
    CConnman* clientConnMan;

    std::chrono::milliseconds lastQSizePrintTime;
    
    /* For avg verify and execution time calculation. */
    unsigned long totalVerifyTime; // in us
    unsigned long totalVerifyCnt; // in us
    unsigned long totalExeTime; // in us

    volatile int lastBlockValidSeq; // the highest block has been verified by our subgroup
    int lastBlockValidSentSeq; // the highest block has been verified by our subgroup and announced to the other group.
    
    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CConnman* connman, CPre_prepare& ppMsg);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck = true);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck = true);

    CPre_prepare assemblePPMsg(const CPbftBlock& pbft_block);
    CPbftMessage assembleMsg(const uint32_t seq); 
    CReply assembleReply(const uint32_t seq, const uint32_t idx, const char exe_res);
    bool checkMsg(CPbftMessage* msg);
    /*return the last executed seq */
    int executeLog();
    void UpdateBlockValidity(const CPbftMessage& msg);

    inline void printQueueSize(){
	    /* log queue size if we have reached the period. */
	    std::chrono::milliseconds current = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	    if (current - lastQSizePrintTime > std::chrono::milliseconds(QSizePrintPeriod)) {
		std::cout << "queue size log, " << current.count() << "," << reqQueue.size() << std::endl; // time stamp is in milliseconds. TODO: change is to seconds for a long throughput test.
		lastQSizePrintTime = current;
	    }
    }

    inline bool isLeader(){
	return pbftID % groupSize == 0;
    }

    inline bool isBlockInOurVerifyGroup(uint32_t seq){
	return ((pbftID & 1) ^ (seq & 1)) == 0;
    }

private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

void ThreadConsensusLogExe();

extern std::unique_ptr<CPbft> g_pbft;
#endif /* PBFT_H */

