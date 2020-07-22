/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft.h
 * Author: l27ren
 *
 * Created on June 16, 2020, 11:15 AM
 */

#ifndef PBFT_H
#define PBFT_H

#include <string>
#include <sys/time.h>
#include "net.h"
#include "validation.h"
#include "pbft_msg.h"
#include "pubkey.h"
#include "key.h"

extern int32_t pbftID;
extern uint32_t thruInterval;

struct LockReply{
    std::map<int32_t, std::vector<CInputShardReply>> lockReply;
    char decision;
};

enum TxType {SINGLE_SHARD, CROSS_SHARD};

struct TxStat{
    TxType type;
    struct timeval startTime;
};


class CPbft{
public:
    static const uint32_t nFaulty = 1;
    static const size_t groupSize = 4;
    CPubKey myPubKey;
    std::vector<CNode*> leaders; // pbft leader
//    CPubKey myPubKey;
    std::unordered_map<int32_t, CPubKey> pubKeyMap;

    std::unordered_map<uint256, std::unordered_set<std::string>, BlockHasher> replyMap; // key is txid, value is a set of senders' addressName

    //std::vector<CInputShardReply> vInputShardReplies;
    std::unordered_map<uint256, LockReply, BlockHasher> inputShardReplyMap; 

    std::unordered_map<uint256, uint256, BlockHasher> txUnlockReqMap; //  key is the digest of a unlock to commit or unlock to abort req, value is txid

    /* <txid, shard_ptr(tx)>
     * Every time we send a cross-shard tx to its input shards, we add an element to this map. 
     * This map is used when the input shards reply and we need to assemble a commit req. This map enables us to figure out the tx given a txid. */
    std::unordered_map<uint256, CTransactionRef, BlockHasher> mapTxid;

    std::unordered_map<uint256, TxStat, BlockHasher> mapTxStartTime;
    uint32_t nCompletedTx;
    struct timeval thruStartTime;

    CPbft();
    bool checkReplySig(const CReply* pReply) const;
private:
    // private ECDSA key used to sign messages
    CKey privateKey;
};

extern std::unique_ptr<CPbft> g_pbft;

#endif /* PBFT_H */

