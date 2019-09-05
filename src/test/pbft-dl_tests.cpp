/*
 * Created by Liuyang Ren on July 16 2019.
 */
#include <util.h>
#include <test/test_bitcoin.h>

#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

#include "init.h"
#include "pbft-dl/pbft2_5.h"
#include "pbft-dl/pbft-dl.h"
#include "pbft-dl/intra_group_msg.h"
#include "pbft/udp_server_client.h"

BOOST_FIXTURE_TEST_SUITE(pbft_dl_tests, TestingSetup)

void sendReq(std::string reqString, int port, UdpClient& pbftClient);

BOOST_AUTO_TEST_CASE(send_commit_list){
// TODO: add group3 of pbftObjs to provide enough localCC to form a globalPC.
    char pRecvBuf[CPbftMessage::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    
    // create a group with 3 nodes and use one node to emulate the leader of another group
    int ports[] = {8350, 8342, 8343, 8344, 8345, 8346}; 

    // group 1 : pbftObj 0, 1, 2.
    CPbft2_5 pbftObj0(ports[0], 0, 0); 
    CPbft2_5 pbftObj1(ports[1], 1, 0); 
    CPbft2_5 pbftObj2(ports[2], 2, 0); 
    // add peer info to their groupmates.
    pbftObj0.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", ports[1], pbftObj1.getPublicKey())));
    pbftObj0.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", ports[2], pbftObj2.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", ports[0], pbftObj0.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", ports[2], pbftObj2.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", ports[0], pbftObj0.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", ports[1], pbftObj1.getPublicKey())));

    // group 2 : pbftObj 3, 4, 5.
    CPbft2_5 pbftObj3(ports[3], 3, 3); 
    CPbft2_5 pbftObj4(ports[4], 4, 3); 
    CPbft2_5 pbftObj5(ports[5], 5, 3); 
    // add peer info to their groupmates.
    pbftObj3.peers.insert(std::make_pair(pbftObj4.server_id, CPbftPeer("localhost", ports[4], pbftObj4.getPublicKey())));
    pbftObj3.peers.insert(std::make_pair(pbftObj5.server_id, CPbftPeer("localhost", ports[5], pbftObj5.getPublicKey())));
    pbftObj4.peers.insert(std::make_pair(pbftObj3.server_id, CPbftPeer("localhost", ports[3], pbftObj3.getPublicKey())));
    pbftObj4.peers.insert(std::make_pair(pbftObj5.server_id, CPbftPeer("localhost", ports[5], pbftObj5.getPublicKey())));
    pbftObj5.peers.insert(std::make_pair(pbftObj3.server_id, CPbftPeer("localhost", ports[3], pbftObj3.getPublicKey())));
    pbftObj5.peers.insert(std::make_pair(pbftObj4.server_id, CPbftPeer("localhost", ports[4], pbftObj4.getPublicKey())));

    // add other group leader info to the leader of the above group
    pbftObj0.dlHandler.peerGroupLeaders.insert({pbftObj3.server_id, CPbftPeer("localhost", ports[3], pbftObj3.getPublicKey())});
    pbftObj3.dlHandler.peerGroupLeaders.insert({pbftObj0.server_id, CPbftPeer("localhost", ports[0], pbftObj0.getPublicKey())});
    pbftObj0.dlHandler.pkMap.insert(std::make_pair(pbftObj3.server_id, pbftObj3.getPublicKey()));
    pbftObj0.dlHandler.pkMap.insert(std::make_pair(pbftObj4.server_id, pbftObj4.getPublicKey()));
    pbftObj0.dlHandler.pkMap.insert(std::make_pair(pbftObj5.server_id, pbftObj5.getPublicKey()));
    pbftObj3.dlHandler.pkMap.insert(std::make_pair(pbftObj0.server_id, pbftObj0.getPublicKey()));
    pbftObj3.dlHandler.pkMap.insert(std::make_pair(pbftObj1.server_id, pbftObj1.getPublicKey()));
    pbftObj3.dlHandler.pkMap.insert(std::make_pair(pbftObj2.server_id, pbftObj2.getPublicKey()));

    std::thread t0(DL_Receive, std::ref(pbftObj0));
    std::thread t1(DL_Receive, std::ref(pbftObj1));
    std::thread t2(DL_Receive, std::ref(pbftObj2));
    std::thread t3(DL_Receive, std::ref(pbftObj3));
    std::thread t4(DL_Receive, std::ref(pbftObj4));
    std::thread t5(DL_Receive, std::ref(pbftObj5));


    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;
    
    for(int i = 0; i < 1; i++){
    	std::string reqString = "r x=" + std::to_string(i); // the format of a request is r followed by the real request
	sendReq(reqString, ports[0], pbftClient);
    }

    t0.join();
}

void sendReq(std::string reqString, int port, UdpClient& pbftClient){
    std::ostringstream oss;
    oss << reqString; // do not put space here as space is used delimiter in stringstream.
    pbftClient.sendto(oss, "localhost", port);
}

BOOST_AUTO_TEST_SUITE_END()
