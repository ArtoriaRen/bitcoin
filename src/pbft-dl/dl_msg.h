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
#include "pbft/pbft_msg.h"

//enum DL_Phase {DLPP_LPP, DLPP_LP, DLPP_LC, DLPP_GPP, DLP_LPP, DLP_LP, DLP_LC, DLP_GP, DLP_GPCD, DLC_GPLC, DLC_GC, DLC_GCCD, DLR_LR, DLR_GR};

enum DL_Phase {DLPP_GPP = PbftPhase::end, DLP_GP, DLP_GPCD, DLC_GPLC, DLC_GC, DLC_GCCD, DLR_LR, DLR_GR};


/* similar to CPbftMesssage but with dl-pbft phases
 */
class DL_Message {
public:
    uint32_t phase;
    uint32_t localView;
    uint32_t globalView;
    uint32_t seq;
    uint32_t senderId;
    uint256 digest; // use the block header hash as digest.
    std::vector<unsigned char> vchSig; //serilized ecdsa signature.
    const static uint32_t messageSizeBytes = 128; // the real size is 4*4 + 32 +72 = 120 bytes.
    
    DL_Message();

    DL_Message(uint32_t senderId);
    
    DL_Message(DL_Phase p, uint32_t senderId);
    
//    DL_Message(CPre_prepare& pre_prepare, uint32_t senderId);
    
    virtual void serialize(std::ostringstream& s, const char* clientReq = nullptr) const;
    
    virtual void deserialize(std::istringstream& s, char* clientReq = nullptr); 

    void getHash(uint256& result);
};



#endif /* DL_MSG_H */

