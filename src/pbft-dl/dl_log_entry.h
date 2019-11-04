/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   dl_log_entry.h
 * Author: l27ren
 *
 * Created on September 2, 2019, 1:39 PM
 */

#ifndef DL_LOG_ENTRY_H
#define DL_LOG_ENTRY_H

#include "pbft-dl/intra_group_msg.h"
#include "pbft-dl/cross_group_msg.h"

class DL_LogEntry{
public:
    DL_Phase phase;
    // data structure for local consenesus
    CLocalPP pre_prepare;
    // Prepare and Commit vectors are only used in local view change. For normal operation, we use the count instead.
//    std::vector<CPrepare> prepareArray;
    uint32_t prepareCount;
//    uint32_t commitCount;
    // holds 2f+1 local commits within the group.
    std::vector<CIntraGroupMsg> localCC; 
    // holds 2f+1 local commits for receiving global prepare certificate.
    std::vector<CIntraGroupMsg> GPLC; 
    // holds 2f+1 local replies for receiving global commit certificate.
    std::vector<CLocalReply> localReplies; 


    // data structure for global consensus
    // a list of 2F+1 LocalCC
    std::deque<CCrossGroupMsg> globalPC; 
    // a list of 2F+1 LocalCC acking a group has received a globalPC.
    std::deque<CCrossGroupMsg> globalCC; 

    // execution result for this log entry
    char result;


    //---placeholder. default phase should be pre-prepare.
    DL_LogEntry();
    DL_LogEntry(uint32_t nFaulty);
    DL_LogEntry(const CLocalPP& pp, uint32_t nFaulty);
};


#endif /* DL_LOG_ENTRY_H */

