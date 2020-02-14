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
void receiveServerReplies();

BOOST_AUTO_TEST_CASE(send_commit_list){
    
    // create groups with 4 nodes and use one node to emulate the leader of another group
    int basePort = 8350;
    const unsigned int numNodes = 28;
    const unsigned int groupSize = 4;
    const unsigned int numGroups = numNodes/groupSize;
    CPbft2_5 pbftObjs[numNodes];
    for(unsigned int i = 0; i < numNodes; i++){
	pbftObjs[i] = CPbft2_5(basePort + i, i, i - i % groupSize, (numGroups - 1)/3); 
    }

    // add peer info to their groupmates.
    for (unsigned int i = 0; i < numGroups; i++) {
	for (unsigned int j = i * groupSize; j < (i + 1) * groupSize; j++) {
	    for (unsigned int k = i * groupSize; k < (i + 1) * groupSize; k++) {
		if(k != j){
		    pbftObjs[j].peers.insert(std::make_pair(pbftObjs[k].server_id, CPbftPeer("localhost", basePort + k, pbftObjs[k].getPublicKey())));

		}
	    }
	}
    }

    // add other group leader info to local leaders 
    for (unsigned int i = 0; i < numNodes; i += groupSize) {
    	for (unsigned int j = 0; j < numNodes; j += groupSize) {
	    if(j != i) {
		    pbftObjs[i].dlHandler.peerGroupLeaders.insert({pbftObjs[j].server_id, CPbftPeer("localhost", basePort + j, pbftObjs[j].getPublicKey())});
	    }
	}
    }

    // add all nodes' pk to each node's pk list 
    for(unsigned int j = 0; j < numNodes; j++){
	for(unsigned int i = 0; i < numNodes; i++){
	    pbftObjs[j].dlHandler.pkMap.insert(std::make_pair(pbftObjs[i].server_id, pbftObjs[i].getPublicKey()));
	}
    }
    
    std::vector<std::thread> threads;
    threads.reserve(numNodes);
    for (uint i = 0; i < numNodes; i++){
	threads.emplace_back(std::thread(DL_Receive, std::ref(pbftObjs[i])));
    }
    std::thread clientUdpReceiver(receiveServerReplies);
    
    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;
    
    for(int i = 0; i < 1; i++){
	// send  a write request
    	std::string reqString = "r w,123,p"; 
	sendReq(reqString, basePort, pbftClient);

	// send  a read request
    	reqString = "r r,123"; 
	sendReq(reqString, basePort, pbftClient);
    }
    
    threads[0].join();
}

void sendReq(std::string reqString, int port, UdpClient& pbftClient){
    std::ostringstream oss;
    oss << reqString; // do not put space here as space is used delimiter in stringstream.
    pbftClient.sendto(oss, "localhost", port);
}

void receiveServerReplies(){
    /* wait for 2F global reply messages.
     * Should use "netcat -ul 2115" to listen for udp packets, otherwise, the t0.join() won't be executed */
    const int nFaultyGroups = 1;
    const int nFaulty = 1;
    char pRecvBuf[(2 * nFaultyGroups + 1) * (2 * nFaulty + 1) * CIntraGroupMsg::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    // we wait for 6 reply msg here because we've send two requests.
    for (int i = 0; i < 6; i++) {
	ssize_t recvBytes = udpServer.recv(pRecvBuf, (2 * nFaultyGroups + 1)*(2 * nFaulty + 1) * CIntraGroupMsg::messageSizeBytes);
	std::string recvString(pRecvBuf, recvBytes);
	std::istringstream iss(recvString);
	int phaseNum = -1;
	iss >> phaseNum;
    	BOOST_CHECK_EQUAL(static_cast<DL_Phase>(phaseNum), DL_Phase::DL_GR);
	CGlobalReply gReply;
	gReply.deserialize(iss);
	std::cout << "reply from server " <<  gReply.localReplyArray[0].senderId << " is: " << gReply.localReplyArray[0].reply << std::endl; 
    }
    std::cout << "get all 6 replies from servers " << std::endl; 

}

BOOST_AUTO_TEST_SUITE_END()
