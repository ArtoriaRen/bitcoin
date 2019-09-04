/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include "pbft-dl/dl_log_entry.h"

DL_LogEntry::DL_LogEntry():phase(DL_pre_prepare), prepareCount(0){
}

DL_LogEntry::DL_LogEntry(uint32_t nFaulty): phase(DL_pre_prepare), prepareCount(0){
    localCC.reserve(2 * nFaulty + 1); 
}

DL_LogEntry::DL_LogEntry(const CLocalPP& pp, uint32_t nFaulty): phase(DL_prepare), pre_prepare(pp), prepareCount(0){
    localCC.reserve(2 * nFaulty + 1); 
    /*log for a pre-prepare message will not be created if the sig is wrong, 
     so the protocol for this entry directly enter Prepare phase once created.
     */
}
