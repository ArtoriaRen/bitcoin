/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbft_log_entry.h
 * Author: l27ren
 *
 * Created on July 8, 2019, 11:56 AM
 */

#ifndef LOG_ENTRY_H
#define LOG_ENTRY_H
#include "msg.h"

class CLogEntry{
public:
    PrePrepareMsg pre_prepare;
    // Prepare and Commit vectors are only used in view change. For normal operation, we use the count instead.
//    std::vector<CPrepare> prepareArray;
//    std::vector<CCommit> commitArray;
    uint32_t prepareCount;
    uint32_t commitCount;

    PbftShardingPhase phase;
    // execution result for this log entry
    char result;
    

    //---placeholder. default phase should be pre-prepare.
    CLogEntry(): prepareCount(0), commitCount(0), phase(PbftShardingPhase::PRE_PREPARE){}
    
    CLogEntry(const PrePrepareMsg& pp):prepareCount(0), commitCount(0){
	pre_prepare = pp;
	// log for a pre-prepare message will not be created if the sig is wrong, so the protocol for this entry directly enter Prepare phase once created.
	phase = PbftShardingPhase::PREPARE;
    }
    
};


#endif /* PBFT_LOG_ENTRY_H */

