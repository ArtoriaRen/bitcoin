/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   cert.h
 * Author: l27ren
 *
 * Created on September 5, 2019, 10:00 AM
 */

#ifndef CERT_H
#define CERT_H

#include "pbft-dl/debug_flags.h"
#include "pbft-dl/cross_group_msg.h"

class CCert{
public:
    DL_Phase phase; // phase should be global PC dessiminate or global CC dessiminate
    uint32_t certSize; // the number of localCCs in the cert. should be 2F+1.
    std::deque<CCrossGroupMsg> globalCert; // a global PC or a global CC: a list of 2F+1 LocalCC
    
    CCert(); 
    CCert(DL_Phase p, uint32_t size); // this constructor is need for deserialization.
    CCert(DL_Phase p, uint32_t size, std::deque<CCrossGroupMsg>& cert);
    
    void serialize(std::ostringstream& s) const;
    
    void deserialize(std::istringstream& s); 
};


#endif /* CERT_H */

