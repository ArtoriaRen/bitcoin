/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft_log_entry.h
 * Author: l27ren
 *
 * Created on June 11, 2020, 11:38 AM
 */

#ifndef PBFT_LOG_ENTRY_H
#define PBFT_LOG_ENTRY_H

#include "util.h"
#include "pbft/pbft_msg.h"
#include <set>
#include <atomic>

class CPbftLogEntry{
public:
    CPre_prepare ppMsg;
    // Prepare and Commit vectors are only used in view change. For normal operation, we use the count instead.
//    std::vector<CPrepare> prepareArray;
//    std::vector<CCommit> commitArray;
    uint32_t prepareCount;
    uint32_t commitCount;

    PbftPhase phase;
    // execution result for this log entry
    uint32_t txCnt;

    std::atomic<bool> blockVerified;
    /* a set of peerId who sent us collab verifying results. */
    std::set<uint32_t> setCollabPeerID;

    //---placeholder. default phase should be pre-prepare.
    CPbftLogEntry(): prepareCount(0), commitCount(0), phase(PbftPhase::pre_prepare), txCnt(0), blockVerified(false) {}
};


#endif /* PBFT_LOG_ENTRY_H */

