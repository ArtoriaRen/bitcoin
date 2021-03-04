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
extern int32_t nWarmUpBlocks;
extern bool testStarted;

class TxIndexOnChain {
public:
    uint32_t block_height;
    uint32_t offset_in_block;

    TxIndexOnChain();
    TxIndexOnChain(const uint32_t block_height_in, const uint32_t offset_in_block_in);
    bool IsNull();

    template<typename Stream>
    void Serialize(Stream& s) const{
	s.write(reinterpret_cast<const char*>(&block_height), sizeof(block_height));
	s.write(reinterpret_cast<const char*>(&offset_in_block), sizeof(offset_in_block));
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
	s.read(reinterpret_cast<char*>(&block_height), sizeof(block_height));
	s.read(reinterpret_cast<char*>(&offset_in_block), sizeof(offset_in_block));
    }

    TxIndexOnChain operator+(const unsigned int oprand);

    friend bool operator<(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator>(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator==(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator!=(const TxIndexOnChain& a, const TxIndexOnChain& b);
    friend bool operator<=(const TxIndexOnChain& a, const TxIndexOnChain& b);

    std::string ToString() const;
};

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

class uint256Hasher
{
public:
    size_t operator()(const uint256& id) const {
        return id.GetCheapHash();
    }
};

class OutpointHasher
{
public:
    size_t operator()(const COutPoint& outpoint) const {
        return outpoint.hash.GetCheapHash() ^ outpoint.n;
    }
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

    volatile int lastBlockVerifiedThisGroup; // the highest block has been verified by our subgroup
    int lastBlockVerifiedOtherSubgroup; // the highest block has been verified by our subgroup
    int lastBlockValidSentSeq; // the highest block has been verified by our subgroup and announced to the other group.
    int lastReplySentSeq; // the highest block we have sent reply to the client. Used only by the msg_hand thread. 

    /* adjancy matrix for dependency graph for unverified tx.
     * Key is an unverified tx; Value is all tx depend on the Key tx
     * (both create-spend and spend-spend dependency).
     * Also used to decide if a tx should be added to the dependency graph
     * due to create-spend dependency.
     */
    std::unordered_map<uint256, std::list<uint256>, uint256Hasher> mapTxDependency;
    /* prerequite tx count map.
     * Key is an unverified tx; Value is the count of the remaining 
     * not-yet-verified prerequite tx. 
     * Used to decide if a tx can be removed from the dependency graph and executed.
     */
    std::unordered_map<uint256, uint32_t, uint256Hasher> mapPrereqCnt;
    /* UTXO conflict list.
     * Key is an UTXO, value is a list of unverified tx spending this UTXO.
     * Used to detect if a tx should be added to the dependency graph due to 
     * spend-spend dependency. 
     */
    std::unordered_map<COutPoint, std::list<uint256>, OutpointHasher> mapUtxoConflict;
    

    CPbft();
    // Check Pre-prepare message signature and send Prepare message
    bool ProcessPP(CConnman* connman, CPre_prepare& ppMsg);

    // Check Prepare message signature, add to corresponding log, check if we have accumulated 2f Prepare message. If so, send Commit message
    bool ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck = true);
    
    // Check Commit message signature, add to corresponding log, check if we have accumulated 2f+1 Commit message. If so, execute transactions and reply. 
    bool ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck = true);

    CPre_prepare assemblePPMsg(std::shared_ptr<CPbftBlock> pPbftBlockIn);
    CPbftMessage assembleMsg(const uint32_t seq); 
    CReply assembleReply(const uint32_t seq, const uint32_t idx, const char exe_res) const;
    bool checkMsg(CPbftMessage* msg);
    /*return the last executed seq */
    void executeLog(struct timeval& start_process_first_block);
    /* when received collab msg from the other subgroup, update our block valid bit.
     * Called by the net_processing theread. */
    bool checkCollabMsg(const CCollabMessage& msg);
    bool AssembleAndSendCollabMsg();
    bool sendReplies(CConnman* connman);

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

    inline bool isBlockInOurVerifyGroup(uint256& block_hash){
	return ((pbftID & 1) ^ (block_hash.GetCheapHash() & 1)) == 0;
    }

    void saveBlocks2File(const int numBlock) const;
    void readBlocksFromFile(const int numBlock);
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

void ThreadConsensusLogExe();

extern std::unique_ptr<CPbft> g_pbft;
#endif /* PBFT_H */

