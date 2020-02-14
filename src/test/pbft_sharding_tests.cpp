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
#include "pbft_sharding/pbft_sharding.h"
#include "pbft_sharding/log_entry.h"
#include "pbft_sharding/msg.h"
#include "pbft/udp_server_client.h"

BOOST_FIXTURE_TEST_SUITE(pbft_sharding_tests, TestingSetup)
	
void sendReq(std::string reqString, int port, UdpClient& pbftClient);
void receiveServerReplies();

BOOST_AUTO_TEST_CASE(pbft_sharding_test){
    /*This test case start a new thread to run udp server for each pbft object. 
     * Must use Ctrl-C to terminate this test.
     */ 

    /* We cannot use a for loop to create CPbft objects and store them in an 
     * array or vector b/c no copy constructor is implemented. Even with pointers,
     * objects created within a loop go out of scope once the control exits the
     * loop and pointers become dangling.
     */
    int basePort = 8350; 
    const unsigned int numNodes = 4;
    CPbftSharding pbftShardingObjs[numNodes];
    for(unsigned int i = 0; i < numNodes; i++){
	pbftShardingObjs[i] = CPbftSharding(basePort + i, i, numNodes); 
    }

    for(uint i = 0; i < numNodes; i++){
	for(uint j = 0; j < numNodes; j++) {
	    if (j != i) {
		pbftShardingObjs[i].peers.insert(std::make_pair(pbftShardingObjs[j].server_id, CPbftPeer("localhost", basePort + j, pbftShardingObjs[j].getPublicKey())));
	    }
	}
    }

    std::vector<std::thread> threads;
    threads.reserve(numNodes);
    for (uint i = 0; i < numNodes; i++){
	threads.emplace_back(std::thread(interruptableReceive, std::ref(pbftShardingObjs[i])));
    }

    std::thread clientUdpReceiver(receiveServerReplies);
    
    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;

    // send  a txn 
    //std::string reqString = "r w,123,p"; 
//    sendReq(reqString, basePort, pbftClient);
//    
    
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
    char pRecvBuf[Message::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    // we wait for 8 reply msg here because we've send two requests.
    for (int i = 0; i < 32; i++) {
	ssize_t recvBytes = udpServer.recv(pRecvBuf, Message::messageSizeBytes);
	std::string recvString(pRecvBuf, recvBytes);
	std::istringstream iss(recvString);
	int phaseNum = -1;
	iss >> phaseNum;
    	BOOST_CHECK_EQUAL(static_cast<PbftShardingPhase>(phaseNum), PbftShardingPhase::REPLY);
	Reply r;
	r.deserialize(iss);
	std::cout << "CLIENT: reply from server " <<  r.senderId << " is: " << r.reply << std::endl; 
    }
    std::cout << "get all 6 replies from servers " << std::endl; 

}
BOOST_AUTO_TEST_SUITE_END()