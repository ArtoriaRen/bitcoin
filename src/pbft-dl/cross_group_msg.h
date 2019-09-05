/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   crossGroupMsg.h
 * Author: l27ren
 *
 * Created on September 2, 2019, 10:07 AM
 */

#ifndef CROSSGROUPMSG_H
#define CROSSGROUPMSG_H
#include "util.h"
#include "pbft-dl/intra_group_msg.h"

class CCrossGroupMsg{
public:
    DL_Phase phase;
    std::vector<CIntraGroupMsg> localCC;
    std::string clientReq;
    
    CCrossGroupMsg(); 
    CCrossGroupMsg(DL_Phase p); // this constructor is need for deserialization.
    CCrossGroupMsg(DL_Phase p, std::vector<CIntraGroupMsg>& commits);
    // This constructor should be called only for GPP construction.
    CCrossGroupMsg(DL_Phase p, std::vector<CIntraGroupMsg>& commits, std::string& req);
    
    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 
};


#endif /* CROSSGROUPMSG_H */

