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
    CPbftMessage ppMsg;
    // Prepare and Commit vectors are only used in view change. For normal operation, we use the count instead.
//    std::vector<CPrepare> prepareArray;
//    std::vector<CCommit> commitArray;
    uint32_t prepareCount;
    uint32_t commitCount;

    std::shared_ptr<CPbftBlock> pPbftBlock;

    /* in case we receive collab msg before we receive a block, use this field to store
     * the block size extracted from the collab msg. */
    uint32_t blockSizeInCollabMsg;

    PbftPhase phase;

    /* the peer IDs in the verification group of this block */
    std::set<uint32_t> vrfGroup;
    /* the peer that we need to send this block to once we received it.*/
    int successorBlockPassing;

    CPbftLogEntry(): prepareCount(0), commitCount(0), pPbftBlock(nullptr), blockSizeInCollabMsg(0), phase(PbftPhase::pre_prepare), successorBlockPassing(-1) {}
};


#endif /* PBFT_LOG_ENTRY_H */

