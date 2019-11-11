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
void receiveServerReplies(CPbft2_5& pbft2_5Obj);

BOOST_AUTO_TEST_CASE(send_commit_list){
    
    // create a group with 3 nodes and use one node to emulate the leader of another group
    int ports[] = {8350, 8342, 8343, 8344, 8345, 8346, 8347, 8348, 8349}; 
    const unsigned int numNodes = 9;
    CPbft2_5 pbftObjs[numNodes];
    for(int i = 0; i < numNodes; i++){
	pbftObjs[i] = CPbft2_5(ports[i], i, (i/3) * 3); 
    }
    
    // add peer info to their groupmates.
    pbftObjs[0].peers.insert(std::make_pair(pbftObjs[1].server_id, CPbftPeer("localhost", ports[1], pbftObjs[1].getPublicKey())));
    pbftObjs[0].peers.insert(std::make_pair(pbftObjs[2].server_id, CPbftPeer("localhost", ports[2], pbftObjs[2].getPublicKey())));
    pbftObjs[1].peers.insert(std::make_pair(pbftObjs[0].server_id, CPbftPeer("localhost", ports[0], pbftObjs[0].getPublicKey())));
    pbftObjs[1].peers.insert(std::make_pair(pbftObjs[2].server_id, CPbftPeer("localhost", ports[2], pbftObjs[2].getPublicKey())));
    pbftObjs[2].peers.insert(std::make_pair(pbftObjs[0].server_id, CPbftPeer("localhost", ports[0], pbftObjs[0].getPublicKey())));
    pbftObjs[2].peers.insert(std::make_pair(pbftObjs[1].server_id, CPbftPeer("localhost", ports[1], pbftObjs[1].getPublicKey())));
    
    // add peer info to their groupmates.
    pbftObjs[3].peers.insert(std::make_pair(pbftObjs[4].server_id, CPbftPeer("localhost", ports[4], pbftObjs[4].getPublicKey())));
    pbftObjs[3].peers.insert(std::make_pair(pbftObjs[5].server_id, CPbftPeer("localhost", ports[5], pbftObjs[5].getPublicKey())));
    pbftObjs[4].peers.insert(std::make_pair(pbftObjs[3].server_id, CPbftPeer("localhost", ports[3], pbftObjs[3].getPublicKey())));
    pbftObjs[4].peers.insert(std::make_pair(pbftObjs[5].server_id, CPbftPeer("localhost", ports[5], pbftObjs[5].getPublicKey())));
    pbftObjs[5].peers.insert(std::make_pair(pbftObjs[3].server_id, CPbftPeer("localhost", ports[3], pbftObjs[3].getPublicKey())));
    pbftObjs[5].peers.insert(std::make_pair(pbftObjs[4].server_id, CPbftPeer("localhost", ports[4], pbftObjs[4].getPublicKey())));
    
    // add peer info to their groupmates.
    pbftObjs[6].peers.insert(std::make_pair(pbftObjs[7].server_id, CPbftPeer("localhost", ports[7], pbftObjs[7].getPublicKey())));
    pbftObjs[6].peers.insert(std::make_pair(pbftObjs[8].server_id, CPbftPeer("localhost", ports[8], pbftObjs[8].getPublicKey())));
    pbftObjs[7].peers.insert(std::make_pair(pbftObjs[6].server_id, CPbftPeer("localhost", ports[6], pbftObjs[6].getPublicKey())));
    pbftObjs[7].peers.insert(std::make_pair(pbftObjs[8].server_id, CPbftPeer("localhost", ports[8], pbftObjs[8].getPublicKey())));
    pbftObjs[8].peers.insert(std::make_pair(pbftObjs[6].server_id, CPbftPeer("localhost", ports[6], pbftObjs[6].getPublicKey())));
    pbftObjs[8].peers.insert(std::make_pair(pbftObjs[7].server_id, CPbftPeer("localhost", ports[7], pbftObjs[7].getPublicKey())));
    
    // add other group leader info to local leaders 
    pbftObjs[0].dlHandler.peerGroupLeaders.insert({pbftObjs[3].server_id, CPbftPeer("localhost", ports[3], pbftObjs[3].getPublicKey())});
    pbftObjs[0].dlHandler.peerGroupLeaders.insert({pbftObjs[6].server_id, CPbftPeer("localhost", ports[6], pbftObjs[6].getPublicKey())});
    pbftObjs[3].dlHandler.peerGroupLeaders.insert({pbftObjs[0].server_id, CPbftPeer("localhost", ports[0], pbftObjs[0].getPublicKey())});
    pbftObjs[3].dlHandler.peerGroupLeaders.insert({pbftObjs[6].server_id, CPbftPeer("localhost", ports[6], pbftObjs[6].getPublicKey())});
    pbftObjs[6].dlHandler.peerGroupLeaders.insert({pbftObjs[0].server_id, CPbftPeer("localhost", ports[0], pbftObjs[0].getPublicKey())});
    pbftObjs[6].dlHandler.peerGroupLeaders.insert({pbftObjs[3].server_id, CPbftPeer("localhost", ports[3], pbftObjs[3].getPublicKey())});
    // all all nodes' pk to each node's pk list 
    for(int j = 0; j < 9; j++){
	for(int i = 0; i < 9; i++){
	    pbftObjs[j].dlHandler.pkMap.insert(std::make_pair(pbftObjs[i].server_id, pbftObjs[i].getPublicKey()));
	}
    }
    
    std::thread t0(DL_Receive, std::ref(pbftObjs[0]));
    std::thread t1(DL_Receive, std::ref(pbftObjs[1]));
    std::thread t2(DL_Receive, std::ref(pbftObjs[2]));
    std::thread t3(DL_Receive, std::ref(pbftObjs[3]));
    std::thread t4(DL_Receive, std::ref(pbftObjs[4]));
    std::thread t5(DL_Receive, std::ref(pbftObjs[5]));
    std::thread t6(DL_Receive, std::ref(pbftObjs[6]));
    std::thread t7(DL_Receive, std::ref(pbftObjs[7]));
    std::thread t8(DL_Receive, std::ref(pbftObjs[8]));
    std::thread t9(receiveServerReplies, std::ref(pbftObjs[0]));
    
    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;
    
    for(int i = 0; i < 1; i++){
	// send  a write request
    	std::string reqString = "r w,123,p"; 
	sendReq(reqString, ports[0], pbftClient);
    }
    
    
    t0.join();
}

void sendReq(std::string reqString, int port, UdpClient& pbftClient){
    std::ostringstream oss;
    oss << reqString; // do not put space here as space is used delimiter in stringstream.
    pbftClient.sendto(oss, "localhost", port);
}

void receiveServerReplies(CPbft2_5& pbft2_5Obj){
    /* wait for 2F global reply messages.
     * Should use "netcat -ul 2115" to listen for udp packets, otherwise, the t0.join() won't be executed */
    int nFaultyGroups = 1;
    int nFaulty = 1;
    char pRecvBuf[(2 * nFaultyGroups + 1) * (2 * nFaulty + 1) * CIntraGroupMsg::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    for (int i = 0; i < 3; i++) {
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

}

BOOST_AUTO_TEST_SUITE_END()
