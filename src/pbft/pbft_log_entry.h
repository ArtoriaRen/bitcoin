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

#ifndef PBFT_LOG_ENTRY_H
#define PBFT_LOG_ENTRY_H
#include "util.h"
#include "pbft/pbft_msg.h"

class CPbftLogEntry{
public:
    CPre_prepare pre_prepare;
    std::vector<CPrepare> prepareArray;
    std::vector<CCommit> commitArray;
    PbftPhase phase;
    

    CPbftLogEntry(){}
    
    CPbftLogEntry(const CPre_prepare& pp){
	pre_prepare = pp;
	// log for a pre-prepare message will not be created if the sig is wrong, so the protocol for this entry directly enter Prepare phase once created.
	phase = PbftPhase::prepare;
    }
    
};


#endif /* PBFT_LOG_ENTRY_H */

