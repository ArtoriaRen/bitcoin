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
#include "netmessagemaker.h"

extern int32_t pbftID;
extern int32_t nMaxReqInFly; 
extern int32_t reqWaitTimeout;
extern size_t maxBlockSize; 
extern int32_t warmUpMemoryPageCache;

enum  STEP {TX_UTXO_EXIST_AND_VALUE = 0, TX_SIG_CHECK, TX_DB_UPDATE,
                LOCK_UTXO_EXIST, LOCK_SIG_CHECK, LOCK_UTXO_SPEND, LOCK_RES_SIGN, LOCK_RES_SEND, LOCK_INPUT_COPY, 
                COMMIT_SIG_CHECK, COMMIT_VALUE_CHECK, COMMIT_UTXO_ADD,
                NUM_STEPS};

enum  INPUT_CNT {TX_INPUT_CNT = 0, LOCK_INPUT_CNT,   
                NUM_INPUT_CNTS};

class ThreadSafeQueue {
public:
    ThreadSafeQueue();
    ~ThreadSafeQueue();

    std::shared_ptr<CClientReq>& front();
    std::deque<std::shared_ptr<CClientReq>> get_all();
    std::deque<std::shared_ptr<CClientReq>> get_upto(size_t max_bytes);
    void pop_front();

    void push_back(const std::shared_ptr<CClientReq>& item);
    void push_back(std::shared_ptr<CClientReq>&& item);
    void push_back(CReqBatch& itemBatch);

    int size();
    bool empty();

private:
    std::deque<std::shared_ptr<CClientReq>> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class CPbft{
public:
    // TODO: may need to recycle log slots for throughput test. Consider deque.
    static const size_t logSize = 100000;  
    static const uint32_t nFaulty = 1;
    static const size_t groupSize = 4;
    static const int32_t clientID = 65; // the pbftID of the client.
    uint32_t localView;
    // pbft log. The index is sequence number.
    std::vector<CPbftLogEntry> log;
    uint32_t nextSeq; // next available seq that has not been attached to any client request.
    int lastExecutedSeq; 
    CPubKey myPubKey;

    CNode* client; // pbft client
    /* all peers in the network: both leaders and followers; both in our committee
     * and other committees. The index is peerID. Any peers whose peerID % groupSize
     * = 0 are leaders of committee peerID/groupSize. */
    std::vector<CNode*> peers;  
    std::unordered_map<int32_t, CPubKey> pubKeyMap;

    /* the number of req that are between pre_prepare and reply phase. 
     * Initially we start pbft consensus for this number of req. Later, once a 
     * req  enters the reply phase, another req at the front of the reqQueue is 
     * added to the pbft log and start consensus process. */
    int nReqInFly; 
    /* a queue storing client req waiting for being processed. */
    ThreadSafeQueue reqQueue;
    /* we need the client conn man to wake up the client listening thread to send
     * reply back the client as soon as possible. */
    CConnman* clientConnMan;

    std::chrono::milliseconds notEnoughReqStartTime;

    uint32_t startBlkHeight;

    /* total execution time and count for Tx, LockReq, COMMIT, and ABORT reqs.
     * For avg execution time calculation. */
    unsigned long totalExeTime[4]; // in us
    uint32_t totalExeCount[4];

    /* detailed execution time and count*/
    unsigned long detailTime[STEP::NUM_STEPS];
    unsigned long inputCount[INPUT_CNT::NUM_INPUT_CNTS];
    uint32_t nInputShardSigs;
    int lastReplySentSeq; // the highest block we have sent reply to the client. Used only by the main thread.
        
    
    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CConnman* connman, CPre_prepare& ppMsg);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck = true);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck = true);

    CPre_prepare assemblePPMsg(const CPbftBlock& pbft_block);
    CPbftMessage assembleMsg(const uint32_t seq); 
    CReply assembleReply(const uint32_t seq, const uint32_t idx, const char exe_res) const;
    CInputShardReply assembleInputShardReply(const uint32_t seq, const uint32_t idx, const char exe_res, const CAmount& inputUtxoValueSum);
    bool checkMsg(CPbftMessage* msg);
    /*return the last executed seq */
    int executeLog();
    void sendReplies(CConnman* connman);

    inline void printQueueSize(){
	std::chrono::milliseconds current = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	std::cout << "queue size log, " << current.count() << "," << reqQueue.size() << std::endl; // time stamp is in milliseconds. TODO: change is to seconds for a long throughput test.
    }

    inline bool timeoutWaitReq(){
	/* log queue size if we have reached the period. */
	std::chrono::milliseconds current = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	//std::cout << "notEnoughReqStartTime  =  " << notEnoughReqStartTime.count() << ", current = " << current.count() << ", reqWaitTimeout = " << reqWaitTimeout << std::endl; 
	if (notEnoughReqStartTime != std::chrono::milliseconds::zero() && current - notEnoughReqStartTime > std::chrono::milliseconds(reqWaitTimeout)) {
	    notEnoughReqStartTime = std::chrono::milliseconds::zero();
	    return true;
	} else { 
	    return false;
	}
    }

    inline void setReqWaitTimer(){
	if (notEnoughReqStartTime == std::chrono::milliseconds::zero()) {
	    notEnoughReqStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
	}
    }

    inline bool isLeader(){
	return pbftID % groupSize == 0;
    }

    inline uint32_t getBlockHeight(uint32_t seq) {
	return startBlkHeight + seq;
    }


    void saveBlocks2File() const;
    int readBlocksFromFile();
    void WarmUpMemoryCache();
private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

void ThreadConsensusLogExe();

extern std::unique_ptr<CPbft> g_pbft;
#endif /* PBFT_H */

