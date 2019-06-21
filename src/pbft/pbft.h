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

#ifndef PBFT_H
#define PBFT_H
#include <unordered_map>
#include "netaddress.h"
#include "util.h"
#include "primitives/block.h"
#include "chain.h"
#include "net.h"

//global view number

enum PbftPhase {pre_prepare, prepare, commit, reply};

class CPbftMessage {
public:
    PbftPhase phase;
    uint32_t view;
    uint32_t seq;
    uint32_t sendeId;
    uint256 digest;
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
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

class CPbftLog{
public:
    CPre_prepare pre_prepare;
    std::vector<CPrepare> prepareArray;
    std::vector<CCommit> commitArray;
    PbftPhase phase;
    

    CPbftLog(){}
    
    CPbftLog(const CPre_prepare& pp){
	pre_prepare = pp;
	// log for a pre-prepare message will not be created if the sig is wrong, so the protocol for this entry directly enter Prepare phase once created.
	phase = PbftPhase::prepare;
    }
    
};

class CPbft : public NetEventsInterface {
public:
    int view;
    // TODO: the key should be of type uint256 which is the type of digest. But we need overwrite hash function and equal function.
    std::unordered_map<std::string, CPbftLog> log;
    std::vector<CService> members;
    CService leader;
    uint32_t nGroups; // number of groups.
    uint32_t nFaulty;
    
    explicit CPbft(CConnman*  connmanIn);    

    void InitializeNode(CNode* pnode) override;
    void FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime) override;
    /** Process protocol messages received from a given node */
    bool ProcessMessages(CNode* pfrom, std::atomic<bool>& interrupt) override;
    /**
    * Send queued protocol messages to be sent to a give node.
    *
    * @param[in]   pto             The node which we are sending messages to.
    * @param[in]   interrupt       Interrupt condition for processing threads
    * @return                      True if there is more work to be done
    */
    bool SendMessages(CNode* pto, std::atomic<bool>& interrupt) override;

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

private:
    CConnman* const connman;
};


#endif /* PBFT_H */

