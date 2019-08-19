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
#include "pbft/pbft_log_entry.h"
#include "pbft/pbft_msg.h"
#include "pbft/udp_server_client.h"

BOOST_FIXTURE_TEST_SUITE(pbft_dl_tests, TestingSetup)

void sendReq(std::string reqString, int port, UdpClient& pbftClient);

BOOST_AUTO_TEST_CASE(send2leader){
    //This test case start a new thread to run udp server for each pbft object. Must use Ctrl-C to terminate this test.
    char pRecvBuf[CPbftMessage::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    
    // We cannot use a for loop to create CPbft objects and store them in an array or vector b/c no copy constructor is implemented. Even with pointers, objects created within a loop go out of scope once the control exits the loop and pointers become dangling.
    int port0 = 8350, port1 = 8342, port2 = 8343; 
    CPbft2_5 pbftObj0(port0, 0, 0); 
    CPbft2_5 pbftObj1(port1, 1, 0); 
    CPbft2_5 pbftObj2(port2, 2, 0); 
    pbftObj0.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    pbftObj0.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", port2, pbftObj2.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", port2, pbftObj2.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    std::thread t0(interruptableReceive, std::ref(pbftObj0));
    std::thread t1(interruptableReceive, std::ref(pbftObj1));
    std::thread t2(interruptableReceive, std::ref(pbftObj2));
    
    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;
    
    for(int i = 0; i < 3; i++){
    	std::string reqString = "r x=" + std::to_string(i); // the format of a request is r followed by the real request
	sendReq(reqString, port0, pbftClient);
    }
    
    for(unsigned int i = 0; i < pbftObj0.nFaulty +1; i++){
	ssize_t recvBytes =  udpServer.recv(pRecvBuf, CPbftMessage::messageSizeBytes);
	std::string recv(pRecvBuf, 0, recvBytes);
	std::cout << "receive = " << recv << std::endl;
	//	BOOST_CHECK_EQUAL(recv, reqString.substr(2));
	//	if (reqString.compare(2, reqString.size(), recv) != 0){
	//	    // received invalid reponse, do not increment counter.
	//	    i--;
	//	}
    }
    std::cout << "received f+1 responses." << std::endl;
    
    t0.join();
    t1.join();
    t2.join();
    // TODO: test if all CPbft instance has set x to 8
}


BOOST_AUTO_TEST_CASE(send_commit_list){
    char pRecvBuf[CPbftMessage::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    
    // create a group with 3 nodes and use one node to emulate the leader of another group
    int port0 = 8350, port1 = 8342, port2 = 8343, port3 = 8344; 
    CPbft2_5 pbftObj0(port0, 0, 0); 
    CPbft2_5 pbftObj1(port1, 1, 0); 
    CPbft2_5 pbftObj2(port2, 2, 0); 
    CPbft2_5 pbftObj3(port3, 3, 0); 
    // add peer info to their groupmates.
    pbftObj0.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    pbftObj0.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", port2, pbftObj2.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", port2, pbftObj2.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    std::thread t0(interruptableReceive, std::ref(pbftObj0));
    std::thread t1(interruptableReceive, std::ref(pbftObj1));
    std::thread t2(interruptableReceive, std::ref(pbftObj2));
    // add other group leader info to the leader of the above group
    pbftObj0.dlHandler.peerGroupLeaders.insert({pbftObj3.server_id, CPbftPeer("localhost", port3, pbftObj3.getPublicKey())});
    std::thread t3(interruptableReceive, std::ref(pbftObj3));


    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;
    
    for(int i = 0; i < 1; i++){
    	std::string reqString = "r x=" + std::to_string(i); // the format of a request is r followed by the real request
	sendReq(reqString, port0, pbftClient);
    }

    
    
    
    t0.join();
}

void sendReq(std::string reqString, int port, UdpClient& pbftClient){
    std::ostringstream oss;
    oss << reqString; // do not put space here as space is used delimiter in stringstream.
    pbftClient.sendto(oss, "localhost", port);
}

BOOST_AUTO_TEST_SUITE_END()
