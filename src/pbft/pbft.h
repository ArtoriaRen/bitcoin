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

class BlockCollabRes{
public:
    /* for each tx, cnt = f+1 means the tx has been deemed valid by f+1 nodes.
     * index is tx index in the block
     */
    std::vector<uint32_t> tx_collab_valid_cnt;
    /* key is tx index, value is collab msg cnt. 
     * cnt = f+1 means the tx has been deemed invalid by f+1 nodes. 
     */
    std::map<uint32_t, uint32_t> map_collab_invalid_cnt;
    uint32_t collab_msg_full_tx_cnt;
    BlockCollabRes();
    BlockCollabRes(uint32_t txCnt);
};

class uint256Hasher
{
public:
    size_t operator()(const uint256& id) const {
        return id.GetCheapHash();
    }
};

class txHasher
{
public:
    size_t operator()(const CTransactionRef tx) const {
        return tx->GetHash().GetCheapHash();
    }
};

class OutpointHasher
{
public:
    size_t operator()(const COutPoint& outpoint) const {
        return outpoint.hash.GetCheapHash() ^ outpoint.n;
    }
};

class PendingTxStatus 
{
public:
    uint32_t remaining_prereq_tx_cnt;
    /* 1---collab valid, 0---not yet collab verified.
     * 2---this is a tx of our subgroup, so collab status does not apply.
     * Because collab-invalid tx can be aborted without waiting for it to be
     * prereq-clear, there is no need to store the collab-invalid status in 
     * this field. */
    char collab_status;  
    PendingTxStatus();
    PendingTxStatus(uint32_t remaining_prereq_tx_cnt_in, char collab_status_in);
};

class InitialBlockExecutionStatus {
public:
    uint32_t height;
    std::deque<uint32_t> dependentTxs;
    InitialBlockExecutionStatus();
    InitialBlockExecutionStatus(uint32_t heightIn, std::deque<uint32_t>&& dependentTxs);
};

class QCollabMulBlkRes {
public:
    std::deque<TxIndexOnChain> validTxs;
    std::deque<TxIndexOnChain> invalidTxs;
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
    /* the highest seq whose previous seqs are all in PBFT REPLY phase */
    volatile int lastConsecutiveSeqInReplyPhase; 
    CPubKey myPubKey;

    CNode* client; // pbft client
    /* all peers in the network: both leaders and followers; both in our committee
     * and other committees. The index is peerID. Any peers whose peerID % groupSize
     * = 0 are leaders of committee peerID/groupSize. */
    std::vector<CNode*> peers;  
    std::unordered_map<int32_t, CPubKey> pubKeyMap;

    int nReqInFly; 
    volatile uint32_t nCompletedTx;
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

    /* the highest block has been verified by our subgroup */
    volatile int lastBlockVerifiedThisGroup; 
    /* key is block id, value is a vector of tx verfication status: 1 ---verified valid
     * by the other subgroup; -1 --- verified invalid by the other subgroup; 
     * 0---not verified by the other subgroup. 
     * This is used by the log-exe thread to decide what tx of an other-subgroup 
     * block should be added to the dependency graph.
     */
    std::map<uint32_t, std::deque<char>> futureCollabVrfedBlocks;

    /* adjancy matrix for dependency graph for unverified tx.
     * Key is an unverified tx; Value is all tx depend on the Key tx
     * (both create-spend and spend-spend dependency).
     * Also used to decide if a tx should be added to the dependency graph
     * due to create-spend dependency.
     */
    std::unordered_map<uint256, std::deque<TxIndexOnChain>, uint256Hasher> mapTxDependency;
    /* prerequite tx count map.
     * Key is an unverified tx; Value is the count of the remaining 
     * not-yet-verified prerequite tx. 
     * Used to decide if a tx can be removed from the dependency graph and executed.
     */
    std::map<TxIndexOnChain, PendingTxStatus> mapPrereqCnt;
    /* UTXO conflict list.
     * Key is an UTXO, value is a list of unverified tx spending this UTXO.
     * Used to detect if a tx should be added to the dependency graph due to 
     * spend-spend dependency. 
     */
    std::unordered_map<COutPoint, std::deque<uint256>, OutpointHasher> mapUtxoConflict;
    
    /* key is block height, value is the collab_valid status of this block. 
     * Used for avoiding process more than necessary Collab Message for a tx.
     */
    std::map<uint32_t, BlockCollabRes> mapBlockCollabRes;
    /* The last block that all its tx has been collab validated. 
     * Used to prune the above map.
     */
    int lastCollabFullBlock; 
    
    std::deque<std::deque<TxIndexOnChain>> qValidTx; 
    std::deque<std::deque<TxIndexOnChain>> qInvalidTx; 
    /* which queue is currently used by the log-exe thread. The other one is used
     * by the net_handling thread. The log-exe thread flips the indice 
     * when the  queue used by the log-exe thread is empty and the one used 
     * by the net_handling thread is not empty. 
     */
    uint32_t validTxQIdx;
    uint32_t invalidTxQIdx; 
    /* guard the queues and the indice. */
    std::mutex mutex4Q;
    /* key is peerID, value is the  CCollabMultiBlockMsg to be sent. */
    std::deque<CCollabMultiBlockMsg> otherSubgroupSendQ;
    /* key is block height, value is the ids of peers in the other subgroup of this block. */
    std::map<uint32_t, std::deque<int32_t>> mapBlockOtherSubgroup;

    std::chrono::milliseconds notEnoughReqStartTime;

    /* a pair of map for swapping between log-exe and bitcoind threads. 
     * in a map, key is the block height, value is a queue of not executed
     * tx in the intial execution of the block (batched execution.) 
     */
    std::deque<std::deque<InitialBlockExecutionStatus>> qNotInitialExecutedTx; 
    /* a pair of deque for swapping between log-exe and bitcoind threads.
     * every tx in the deque is executed from the dependency graph. */
    std::deque<std::deque<TxIndexOnChain>> qExecutedTx; 
    /* which queue is currently used by the log-exe thread. The other one is used
     * by the bitcoind thread. The bitcoind thread flips the indice 
     * when the  queue used by the log-exe thread is empty and the one used 
     * by the net_handling thread is not empty. 
     */
    uint32_t notExecutedQIdx;
    uint32_t executedQIdx; 
    /* guard the queues and the indice. */
    std::mutex mutex4ExecutedTx;

    /*-----swapping queue between log-exe thread and bitcoind thread for 
     * collab result sending. The bitcoind thread is responsible for swapping
     * the queues. -----*/
    std::deque<std::deque<CCollabMessage>> qCollabMsg;
    std::deque<QCollabMulBlkRes> qCollabMulBlkMsg;
    /* which queue is being used by the log-exe thread. */
    uint32_t collabMsgQIdx;
    uint32_t collabMulBlkMsgQIdx; 
    /* guard the queues and the indice. */
    std::mutex mutexCollabMsgQ;

    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CConnman* connman, CPre_prepare& ppMsg);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck = true);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck = true);

    CPre_prepare assemblePPMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn);
    CPbftMessage assembleMsg(const uint32_t seq); 
    CReply assembleReply(std::deque<uint256>& vTx, const char exe_res) const;
    bool checkMsg(CPbftMessage* msg);
    bool havePrereqTxCollab(uint32_t height, uint32_t txSeq, std::unordered_set<uint256, uint256Hasher>& preReqTxs, bool alreadyInGraph);
    void addTx2GraphAsDependent(uint32_t height, uint32_t txSeq, std::unordered_set<uint256, uint256Hasher>& preReqTxs);
    void addTx2GraphAsPrerequiste(CTransactionRef pTx);
    void executeLog(struct timeval& start_process_first_block);
    void executePrereqTx(const TxIndexOnChain& txIdx, std::vector<TxIndexOnChain>& validTxs, std::vector<TxIndexOnChain>& invalidTxs);
    void informReplySendingThread(uint32_t height, std::deque<uint32_t>& qDependentTx);
    /* when received collab msg from the other subgroup, update our block valid bit.
     * Called by the net_processing theread. */
    void UpdateTxValidity(const CCollabMessage& msg);
    void UpdateTxValidity(const CCollabMultiBlockMsg& msg);
    bool checkCollabMsg(const CCollabMessage& msg);
    bool checkCollabMulBlkMsg(const CCollabMultiBlockMsg& msg);
    bool SendCollabMsg();
    bool SendCollabMultiBlkMsg(); 
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

    inline bool isInVerifySubGroup(int32_t peer_id, const uint256& block_hash){
	return ((peer_id & 1) ^ (block_hash.GetCheapHash() & 1)) == 0;
    }

    void saveBlocks2File() const;
    int readBlocksFromFile();
    void WarmUpMemoryCache();

    inline void logServerSideThruput(struct timeval& start_process_first_block, struct timeval& end_time, int seq) {
	if (seq == 0) {
	    start_process_first_block = end_time;
	} else if (seq == nWarmUpBlocks - 2) {
	    unsigned long time_us = (end_time.tv_sec - start_process_first_block.tv_sec) * 1000000 + (end_time.tv_usec -  start_process_first_block.tv_usec);
	    std::cout << "Process " << nCompletedTx << " tx in " << time_us << " us. Throughput = " << 1000000 * nCompletedTx / time_us  << " tx/sec."  << std::endl;
	}
    }

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

