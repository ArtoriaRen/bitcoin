/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   dl_msg.h
 * Author: l27ren
 *
 * Created on August 19, 2019, 9:07 AM
 */

#ifndef DL_MSG_H
#define DL_MSG_H

#include <stdint.h>
#include "uint256.h"
#include "util.h"

enum DL_Phase {DL_pre_prepare, DL_prepare, DL_commit, DL_GPP, DL_GP, DL_GPCD, DL_GPLC, DL_GC, DL_GCCD, DL_LR, DL_GR};

//enum DL_Phase {DLPP_GPP = PbftPhase::end, DLP_GP, DLP_GPCD, DLC_GPLC, DLC_GC, DLC_GCCD, DLR_LR, DLR_GR};

class CLocalPP;

/* similar to CPbftMesssage but with dl-pbft phases.
 */
class CIntraGroupMsg {
public:
    uint32_t phase;
    uint32_t localView;
    uint32_t globalView;
    uint32_t seq;
    uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    const static uint32_t messageSizeBytes = 128; // the real size is 4*5 + 32 +72 = 124 bytes.
    
    CIntraGroupMsg();

    CIntraGroupMsg(uint32_t senderId);
    
    CIntraGroupMsg(DL_Phase p, uint32_t senderId);
    
    CIntraGroupMsg(CLocalPP& pre_prepare, uint32_t senderId);
    
    void serialize(std::ostringstream& s, const char* clientReq = nullptr) const;
    
    void deserialize(std::istringstream& s, char* clientReq = nullptr); 

    void getHash(uint256& result);
};

/*Local pre-prepare message*/
class CLocalPP : public CIntraGroupMsg{
public:
    CLocalPP():CIntraGroupMsg(){
    }
    
    CLocalPP(const CIntraGroupMsg& msg);

    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 

    std::string clientReq;
};

/*Local pre-prepare message*/
class CLocalReply {
public:
    uint32_t phase;
    uint32_t senderId;
    char reply; // execution result
    uint256 digest; // use the block header hash as digest.
    /* TODO: change the YCSB workload (probably hash each key and value to constant size)
     * so that the reply has a fixed size.
     * Assume the reply is 1 byte for now.
     */
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    // the real size of a reply msg is 4*2 + 1 + 32 + 72 = 113 bytes.

    CLocalReply(const uint32_t sender, char rpl, const uint256& dgt);

    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 

    void getHash(uint256& result);
};
#endif /* DL_MSG_H */

