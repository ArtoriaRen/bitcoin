/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <locale>
#include<string.h>
#include <netinet/in.h>
#include "pbft/pbft.h"
#include "validation.h"
#include "pbft/pbft_msg.h"
#include "crypto/aes.h"
#include "consensus/validation.h"
#include "consensus/params.h"
#include "consensus/tx_verify.h"
#include "coins.h"
#include "script/interpreter.h"
#include "undo.h"
#include "netmessagemaker.h"
#include <memory>
#include "tx_placement/tx_placer.h"
#include "netmessagemaker.h"
#include "streams.h"
#include "clientversion.h"
#include "init.h"

extern std::unique_ptr<CConnman> g_connman;

bool fIsClient; // if this node is a pbft client.
std::string leaderAddrString;
std::string clientAddrString;
int32_t pbftID; 
int32_t nMaxReqInFly; 
int32_t reqWaitTimeout = 1000;
size_t maxBlockSize = 1000000; //max block size is 1MB of tx (req type, etc. excluded)
int32_t warmUpMemoryPageCache = 1;

ThreadSafeQueue::ThreadSafeQueue() { }

ThreadSafeQueue::~ThreadSafeQueue() { }

TypedReq& ThreadSafeQueue::front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    return queue_.front();
}

std::deque<TypedReq> ThreadSafeQueue::get_all() {
    std::unique_lock<std::mutex> mlock(mutex_);
    std::deque<TypedReq> ret(queue_);
    queue_.clear();
    return ret;
}

std::deque<TypedReq> ThreadSafeQueue::get_upto(size_t max_bytes) {
    std::unique_lock<std::mutex> mlock(mutex_);
    size_t packageSize = 0;
    int i = 0;
    for (; i < queue_.size() && packageSize < max_bytes; i++) {
	CMutableTransaction& tx_mutable = queue_[i].pReq->tx_mutable;
	//std::cout << "tx " << tx_mutable.GetHash().GetHex() << " size = " << ::GetSerializeSize(tx_mutable, SER_NETWORK, PROTOCOL_VERSION) << std::endl; 
	packageSize += ::GetSerializeSize(tx_mutable, SER_NETWORK, PROTOCOL_VERSION);
    }
    //std::cout << "i = " << i << ", packageSize = " << packageSize << std::endl;

    std::deque<TypedReq> ret(queue_.begin(), queue_.begin() + i);
    queue_.erase(queue_.begin(), queue_.begin() + i);
    return ret;
}

void ThreadSafeQueue::pop_front() {
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty()) {
        cond_.wait(mlock);
    }
    queue_.pop_front();
}

void ThreadSafeQueue::push_back(const TypedReq& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

void ThreadSafeQueue::push_back(TypedReq&& item) {
    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(std::move(item));
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}

void ThreadSafeQueue::push_back(CReqBatch& itemBatch) {
    std::unique_lock<std::mutex> mlock(mutex_);
    for (uint i = 0; i < itemBatch.vReq.size(); i++) {
        queue_.push_back(std::move(itemBatch.vReq[i]));
    }
    mlock.unlock(); // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread
}


int ThreadSafeQueue::size() {
    std::unique_lock<std::mutex> mlock(mutex_);
    int size = queue_.size();
    mlock.unlock();
    return size;
}

bool ThreadSafeQueue::empty() {
    std::unique_lock<std::mutex> mlock(mutex_);
    return queue_.empty();
}

CPbft::CPbft() : localView(0), log(std::vector<CPbftLogEntry>(logSize)), nextSeq(0), lastExecutedSeq(-1), client(nullptr), peers(std::vector<CNode*>(groupSize * num_committees)), nReqInFly(0), clientConnMan(nullptr), notEnoughReqStartTime(std::chrono::milliseconds::zero()), startBlkHeight(0), lastReplySentSeq(-1), privateKey(CKey()) {
    privateKey.MakeNewKey(false);
    myPubKey= privateKey.GetPubKey();
    pubKeyMap.insert(std::make_pair(pbftID, myPubKey));
    for (int i = 0; i < 4; i++) {
	totalExeTime[i] = 0;
	totalExeCount[i] = 0;
    }
    for (int i = 0; i < STEP::NUM_STEPS; i++) {
        detailTime[i] = 0;
    }
    for (int i = 0; i < INPUT_CNT::NUM_INPUT_CNTS; i++) {
        inputCount[i] = 0;
    }
}


bool CPbft::ProcessPP(CConnman* connman, CPre_prepare& ppMsg) {
    // sanity check for signature, seq, view, digest.
    /*Faulty nodes may proceed even if the sanity check fails*/
    if (!checkMsg(&ppMsg)) {
        return false;
    }

    // check if the digest matches client req
    ppMsg.pbft_block.ComputeHash();
    if (ppMsg.digest != ppMsg.pbft_block.hash) {
	std::cerr << "digest does not match block hash tx. block hash = " << ppMsg.pbft_block.hash.GetHex() << ", but digest = " << ppMsg.digest.GetHex() << std::endl;
	return false;
    }
    // add to log
    log[ppMsg.seq].ppMsg = ppMsg;
    /* check if at least 2f prepare has been received. If so, enter commit phase directly; otherwise, enter prepare phase.(The goal of this operation is to tolerate network reordering.)
     -----Placeholder: to tolerate faulty nodes, we must check if all prepare msg matches the pre-prepare.
     */


    /* Enter prepare phase. */
    log[ppMsg.seq].phase = PbftPhase::prepare;
    /* make a pMsg */
    CPbftMessage pMsg = assembleMsg(ppMsg.seq);
    /* send the pMsg to other peers, including the leader. */
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    uint32_t start_peerID = pbftID - pbftID % groupSize;
    uint32_t end_peerID = start_peerID + groupSize;
    for (uint32_t i = start_peerID; i < end_peerID; i++) {
	/* do not send a msg to myself. */
	if (i == pbftID)
	    continue; 
	connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::PBFT_P, pMsg));
    }
    /* add the pMsg to our own log.
     * We should do this after we send the pMsg to other peers so that we always 
     * send pMsg before cMsg. 
     */
    ProcessP(connman, pMsg, false);
    return true;
}

bool CPbft::ProcessP(CConnman* connman, CPbftMessage& pMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(&pMsg)) {
	    return false;
	}

	// check the message's view mactches the ppMsg's view in log.
	if (log[pMsg.seq].ppMsg.view != pMsg.view) {
	    std::cerr << "log entry view = " << log[pMsg.seq].ppMsg.view 
		    << ", but msg view = " << pMsg.view << std::endl;
	    return false;
	}
    }

    /* add to log (currently use placeholder: should add the entire message 
     * to log and not increase re-count if the sender is the same. 
     * Also, if prepares are received earlier than pre-prepare, different
     * prepare may have different digest. Should categorize buffered prepares 
     * based on digest.)
     */
    // log[prepare.seq].prepareArray.push_back(prepare);

    /* count the number of prepare msg. enter commit if greater than 2f */
    log[pMsg.seq].prepareCount++;
    /* In the if condition, we use >= (nFaulty << 1) withou worrying about 
     * sending more than one cMsg beacuse the log phase will turn to commit 
     * at the first time the if conditions are met. Thus the code in the if
     * block will not be executed more than once.
     * We use >= instead of == to tolerate receiving more than enough pMsg 
     * before the ppMsg arrives.
     */
    if (log[pMsg.seq].phase == PbftPhase::prepare && log[pMsg.seq].prepareCount >= (nFaulty << 1)) {
	/* Enter commit phase. */
        log[pMsg.seq].phase = PbftPhase::commit;
	/* make a cMsg */
	CPbftMessage cMsg = assembleMsg(pMsg.seq);
	/* send the cMsg to other peers */
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	uint32_t start_peerID = pbftID - pbftID % groupSize;
	uint32_t end_peerID = start_peerID + groupSize;
	for (uint32_t i = start_peerID; i < end_peerID; i++) {
	    /* do not send a msg to myself. */
	    if (i == pbftID)
		continue; 
	    connman->PushMessage(peers[i], msgMaker.Make(NetMsgType::PBFT_C, cMsg));
	}
	/* add the cMsg to our own log.
	 * We should do this after we send the cMsg to other peers so that we always 
	 * send cMsg before execute tx. 
	 */
	ProcessC(connman, cMsg, false);
    }
    return true;
}

bool CPbft::ProcessC(CConnman* connman, CPbftMessage& cMsg, bool fCheck) {
    /* do not perform checking when a peer add a msg to its own log */
    if (fCheck) {
	// sanity check for signature, seq, and the message's view equals the local view.
	if (!checkMsg(&cMsg)) {
	    return false;
	}

	// check the message's view mactches the ppMsg's view in log.
	if (log[cMsg.seq].ppMsg.view != cMsg.view) {
	    std::cerr << "log entry view = " << log[cMsg.seq].ppMsg.view 
		    << ", but msg view = " << cMsg.view << std::endl;
	    return false;
	}
    }

    // count the number of commit msg. enter execute if greater than 2f+1
    log[cMsg.seq].commitCount++;
    if (log[cMsg.seq].phase == PbftPhase::commit && log[cMsg.seq].commitCount >= (nFaulty << 1) + 1) {
        // enter reply phase
        log[cMsg.seq].phase = PbftPhase::reply;
        /* the log-exe thread will execute blocks in reply phase sequentially. */
    }
    return true;
}

bool CPbft::checkMsg(CPbftMessage* msg) {
    // verify signature and return wrong if sig is wrong
    auto it = pubKeyMap.find(msg->peerID);
    if (it == pubKeyMap.end()) {
        std::cerr << "no pub key for sender " << msg->peerID << std::endl;
        return false;
    }
    uint256 msgHash;
    msg->getHash(msgHash);
    if (!it->second.Verify(msgHash, msg->vchSig)) {
        std::cerr << "verification sig fail" << std::endl;
        return false;
    }
    // server should be in the view
    if (localView != msg->view) {
        std::cerr << "server view = " << localView << ", but msg view = " << msg->view << std::endl;
        return false;
    }

    /* check if the seq is alreadly attached to another digest. Checking if log entry is null is necessary b/c prepare msgs may arrive earlier than pre-prepare.
     * Placeholder: Faulty followers may accept.
     */
    if (!log[msg->seq].ppMsg.digest.IsNull() && log[msg->seq].ppMsg.digest != msg->digest) {
        std::cerr << "digest error for log entry " << msg->seq << ". digest in log = " << log[msg->seq].ppMsg.digest.GetHex() << ", but msg->digest = " << msg->digest.GetHex() << std::endl;
        return false;
    }


    return true;
}

CPre_prepare CPbft::assemblePPMsg(const CPbftBlock& pbft_block) {
    CPre_prepare toSent; // phase is set to Pre_prepare by default.
    toSent.seq = nextSeq++;
    toSent.view = 0;
    localView = 0; // also change the local view, or the sanity check would fail.
    toSent.pbft_block = pbft_block;
    toSent.digest = toSent.pbft_block.hash;
    uint256 hash;
    toSent.getHash(hash); // this hash is used for signature, so client tx is not included in this hash.
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CPbftMessage CPbft::assembleMsg(const uint32_t seq) {
    CPbftMessage toSent(log[seq].ppMsg);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CReply CPbft::assembleReply(const uint32_t seq, const uint32_t idx, const char exe_res) const {
    /* 'y' --- execute sucessfully
     * 'n' --- execute fail
     */
    CReply toSent(exe_res, log[seq].ppMsg.pbft_block.vReq[idx].pReq->GetDigest());
    //uint256 hash;
    //toSent.getHash(hash);
    //privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

CInputShardReply CPbft::assembleInputShardReply(const uint32_t seq, const uint32_t idx, const char exe_res, const CAmount& inputUtxoValueSum) {
    /* Strictly speaking, the reply should use log[seq].ppMsg.digest, aka the hash of the 
     * OMNI_LOCK req as the digestIn argument, but the client would need to create another map 
     * mapping OMNI_LOCK req hash to txid. Since we trust the client to be the
     * 2PC leader, we believe it would not send a TxReq or another LockReq for the same tx. 
     * Otherwise, these req would have the same digest field of the reply. */
    CInputShardReply toSent(exe_res, log[seq].ppMsg.pbft_block.vReq[idx].pReq->GetHash(), inputUtxoValueSum);
    uint256 hash;
    toSent.getHash(hash);
    privateKey.Sign(hash, toSent.vchSig);
    toSent.sigSize = toSent.vchSig.size();
    return toSent;
}

int CPbft::executeLog() {
    /* execute all lower-seq tx until this one if possible. */
    CCoinsViewCache view(pcoinsTip.get());
    /* We should go on to execute all log slots that are in reply phase even
     * their seqs are greater than the seq passed in. If we only execute up to
     * the seq passed in, a slot missing a pbftc msg might permanently block
     * log slots after it to be executed. */
    for (uint i = lastExecutedSeq + 1; i < logSize && log[i].phase == PbftPhase::reply; i++) {
	log[i].txCnt = log[i].ppMsg.pbft_block.Execute(i, g_connman.get(), view);
	lastExecutedSeq = i;
	std::cout << "Executed block " << i  << ", block size = " 
		<< log[i].ppMsg.pbft_block.vReq.size()  
		<< ", Total TX cnt = " << totalExeCount[0]
		<< ", Total LOCK cnt = " << totalExeCount[1]
		<< ", Total COMMIT cnt = " << totalExeCount[2]

		<< std::endl;
    }
    bool flushed = view.Flush(); // flush to pcoinsTip
    assert(flushed);
    return lastExecutedSeq;
}

void CPbft::saveBlocks2File() const {
    FILE* file = fsbridge::fopen("pbft_blocks_tx_placement_smart.out", "wb+");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to write." << std::endl;
        return;
    }
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);

    fileout.write((char*)&lastExecutedSeq, sizeof(lastExecutedSeq));
    for (int i = 0; i <= lastExecutedSeq; i++) {
        log[i].ppMsg.pbft_block.Serialize(fileout);
    }
}

int CPbft::readBlocksFromFile() {
    FILE* file = fsbridge::fopen("pbft_blocks_tx_placement_smart.out", "rb");
    if (!file) {
        std::cerr << "Unable to open PBFT block file to read." << std::endl;
        return 0;
    }
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);

    int lastExecutedSeqWarmUp = 0;
    filein.read((char*)&lastExecutedSeqWarmUp, sizeof(lastExecutedSeqWarmUp));
    std::cout << __func__ << ": lastExecutedSeqWarmUp = " << lastExecutedSeqWarmUp << std::endl;
    for (int i = 0; i < lastExecutedSeqWarmUp + 1; i++) {
        try {
            log[i].ppMsg.pbft_block.Unserialize(filein);
        }
        catch (const std::exception& e) {
            std::cerr << "Deserialize or I/O error when reading PBFT block " << i << ": " << e.what() << std::endl;
        }
    }
    return lastExecutedSeqWarmUp;
}

void CPbft::WarmUpMemoryCache() {
    CCoinsViewCache view_tenta(pcoinsTip.get());
    int lastExecutedSeqWarmUp = readBlocksFromFile();
    uint32_t nWarmUpTx = 0;
    for (int i = 0; i <= lastExecutedSeqWarmUp; i++) {
	uint32_t block_size = log[i].ppMsg.pbft_block.vReq.size();
	//std::cout << "executing warm-up block of size " << block_size << std::endl;

        log[i].ppMsg.pbft_block.WarmUpExecute(i, view_tenta);
        /* Discard the block to prepare for performance test. */
        log[i].ppMsg.pbft_block.Clear();
	nWarmUpTx += block_size; 
    }
    std::cout << "warm up -- total executed tx: " << nWarmUpTx << std::endl;
}


void ThreadConsensusLogExe() {
    RenameThread("log-exe");
    while (!ShutdownRequested()) {
        g_pbft->executeLog();
        MilliSleep(50);
    }
}

void CPbft::sendReplies(CConnman* connman) {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    while (lastExecutedSeq > lastReplySentSeq) {
        /* sent reply msg for all executed blocks. */
	int seq = ++lastReplySentSeq;
	std::vector<TypedReq>& vReq = log[seq].ppMsg.pbft_block.vReq;
        const uint num_tx = vReq.size();
	//std::cout << "sending reply for block " << seq <<",  lastReplySentSeq = "<<  lastReplySentSeq << ", lastExecutedSeq = " << lastExecutedSeq << std::endl;
	/*send reply for non-LOCK requests in the block. (Because we are doing closed-loop test, there should be no ABORT request.)*/
        for (uint i = 0; i < num_tx; i++) {
	    if (vReq[i].type != ClientReqType::LOCK) {
		/* hard code execution result as 'y' since we are replaying tx on Bitcoin's chain. */
		CReply reply = assembleReply(seq, i,'y');
		connman->PushMessage(client, msgMaker.Make(NetMsgType::PBFT_REPLY, reply));
	    }
        }
    }
}

std::unique_ptr<CPbft> g_pbft;
/* In case we receive an omniledger unlock_to_abort req, store a copy of all locked coins
 * so that they can be added back to pcoinsTip. */
std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> lockedCoinMap;
