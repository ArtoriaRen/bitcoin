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
extern int32_t QSizePrintPeriod;
extern int32_t maxBlockSize; 
extern int32_t nWarmUpBlocks;
extern bool testStarted;
extern int32_t reqWaitTimeout;
extern size_t groupSize;
extern uint32_t nFaulty;

class ThreadSafeQueue {
public:
    ThreadSafeQueue();
    ~ThreadSafeQueue();

    CTransactionRef& front();
    std::deque<CTransactionRef> get_all();
    std::deque<CTransactionRef> get_upto(uint32_t upto);
    void pop_front();

    void push_back(const CTransactionRef& item);
    void push_back(CTransactionRef&& item);

    int size();
    bool empty();

private:
    std::deque<CTransactionRef> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class ThruputLogger{
private:
    struct timeval lastLogTime;
    uint32_t lastCompletedTxCnt;

public:
    std::stringstream thruputSS;
    void logServerSideThruput(struct timeval& curTime, uint32_t completedTxCnt);
};

class CPbft{
public:
    // TODO: may need to recycle log slots for throughput test. Consider deque.
    static const size_t logSize = 6000;
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
    
    /* total execution time and count for Tx, LockReq, COMMIT, and ABORT reqs.
     * For avg execution time calculation. */
    unsigned long totalExeTime; // in us

    int lastReplySentSeq; // the highest block we have sent reply to the client. Used only by the msg_hand thread. 

    std::chrono::milliseconds notEnoughReqStartTime;

    /* server-side thruput logger. */
    ThruputLogger thruputLogger;

    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CConnman* connman, CPbftMessage& ppMsg);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck = true);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck = true);

    CBlockMsg assembleBlkMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn, uint32_t seq);
    CPbftMessage assemblePPMsg(uint256& block_hash);
    CPbftMessage assembleMsg(const uint32_t seq); 
    CReply assembleReply(std::deque<uint256>& vTx, const char exe_res) const;
    bool checkBlkMsg(CBlockMsg& msg);
    bool checkMsg(CPbftMessage* msg);
    /*return the last executed seq */
    bool executeLog(struct timeval& start_process_first_block);
    bool sendReplies(CConnman* connman);

    bool timeoutWaitReq();
    void setReqWaitTimer();

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

    /* compute the successor on the propagation chain of this block */
    void computesSuccessor(const uint256& block_hash, const uint32_t height);

    void saveBlocks2File() const;
    int readBlocksFromFile();
    void WarmUpMemoryCache();
    void CountInputUTXO();
private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

inline struct timeval operator+(const struct timeval& t0, const struct timeval& t1) {
    struct timeval t = {t0.tv_sec + t1.tv_sec, t0.tv_usec + t1.tv_usec};
    if (t.tv_usec >= 1000000) { // carry needed
        t.tv_sec++;
        t.tv_usec -= 1000000;
    }
    return t;
}

inline struct timeval operator-(const struct timeval& t0, const struct timeval& t1) {
    struct timeval t = {t0.tv_sec - t1.tv_sec, t0.tv_usec - t1.tv_usec};
    if (t.tv_usec < 0) { // borrow needed
        t.tv_sec--;
        t.tv_usec += 1000000;
    }
    return t;
}

inline struct timeval operator+=(struct timeval& t0, const struct timeval& t1) {
    t0 = t0 + t1;
    return t0;
}

void ThreadConsensusLogExe();

extern std::unique_ptr<CPbft> g_pbft;
#endif /* PBFT_H */

