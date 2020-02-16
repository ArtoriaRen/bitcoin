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
    /*  We use two prepare arrays to store commit vote and abort vote separately. 
     * In order to tolerate network reordering, we may receive prepare msg early 
     * than pre-prepare msg, and some messages might vote for commit whereas other 
     * messages vote for abort. Thus we'd better maintain two vectors, otherwise we 
     * may have to extract a abort certificate from one vector. 
     * 
     * Once we receive a pre-prepare message, the vector voting for different decision
     * can be cleaned.
     */
    std::vector<Message> prepareArray; // we must keep PrepareMsg for view change purpose.
    std::vector<Message> prepareAbortArray; // we must keep PrepareMsg for view change purpose.
    std::vector<Message> commitArray; // we must keep CommitMsg for cross-partition purpose.
    std::vector<Message> commitAbortArray; // we must keep CommitMsg for cross-partition purpose.
    //uint32_t prepareCount;
    //uint32_t commitCount;

    PbftShardingPhase phase;
    // execution result for this log entry
    char result;
    

    //---placeholder. default phase should be pre-prepare.
    CLogEntry():phase(PbftShardingPhase::PRE_PREPARE){}
    
    CLogEntry(const PrePrepareMsg& pp){
	pre_prepare = pp;
	// log for a pre-prepare message will not be created if the sig is wrong, so the protocol for this entry directly enter Prepare phase once created.
	phase = PbftShardingPhase::PREPARE;
    }
    
};


#endif /* PBFT_LOG_ENTRY_H */

