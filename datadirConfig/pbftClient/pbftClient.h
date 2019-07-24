/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   pbftClient.h
 * Author: l27ren
 *
 * Created on July 24, 2019, 10:14 AM
 */

#ifndef PBFTCLIENT_H
#define PBFTCLIENT_H


class CPbftClient{
private:
    int val;
    int seq; // use seq to replace timestamp in PBFT
    
    UdpServer udpServer;
    UdpClient udpClient;
    char* pRecvBuf;
    std::string pbftLeaderIP;
    int pbftLeaderPort;

    
public:
    CPbftClient(const std::string pbftClientIp, int pbftClientPort, const std::string pbftLeaderIp, int pbftLeaderPort);
    float testThroughput(uint nReq);
    float testLatency(uint nReq);
    // start a thread to receive result packets. 
    void start();
};


#endif /* PBFTCLIENT_H */

