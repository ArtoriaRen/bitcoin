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

class DL_LogEntry{
public:
    DL_Phase phase;
    // data structure for local consenesus
    CLocalPP pre_prepare;
    // Prepare and Commit vectors are only used in local view change. For normal operation, we use the count instead.
//    std::vector<CPrepare> prepareArray;
    uint32_t prepareCount;
    uint32_t commitCount;

    // data structure for global consensus
    std::list<std::list<CCommit>> globalPC; // a list of 2F+1 LocalCC
    std::list<std::list<CCommit>> globalCC; // a list of 2F+1 LocalCC acking a group has received a globalPC.

    
    

    //---placeholder. default phase should be pre-prepare.
    DL_LogEntry(): phase(DL_pre_prepare), prepareCount(0), commitCount(0){}
    
    DL_LogEntry(const CLocalPP& pp): phase(DL_prepare), pre_prepare(pp), prepareCount(0), commitCount(0){
	/*log for a pre-prepare message will not be created if the sig is wrong, 
	so the protocol for this entry directly enter Prepare phase once created.
	*/
    }
};


#endif /* DL_LOG_ENTRY_H */

