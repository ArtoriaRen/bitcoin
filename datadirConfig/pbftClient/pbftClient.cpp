/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "pbftClient.h"
#include "pbft/udp_server_client.h"
#include "pbft/pbft_msg.h"

CPbftClient::CPbftClient(const std::string cIp, int cPort, const std::string lIp, int lPort):udpServer(UdpServer(cIp, cPort)), udpClient(UdpClient()), pbftLeaderIP(lIp), pbftLeaderPort(lPort) {
    pRecvBuf = new char[CPbftMessage::messageSizeBytes];
}

float CPbftClient::testThroughput(uint nReq){
    
    
}
