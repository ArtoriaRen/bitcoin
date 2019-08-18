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
#include "pbft/pbft.h"
#include "pbft/pbft_log_entry.h"
#include "pbft/pbft_msg.h"
#include "pbft/udp_server_client.h"

BOOST_FIXTURE_TEST_SUITE(pbft_tests, TestingSetup)
	
void sendReq(std::string reqString, int port, UdpClient& pbftClient);

BOOST_AUTO_TEST_CASE(conflict_digest)
{
    // a server should not be able to accept conflicting pre-prepare
    CPbft pbftObj(18322, 0); // 18322 can be an arbitrary port because we do not start UDP server in this test.
    pbftObj.peers.insert(std::make_pair(pbftObj.server_id, CPbftPeer("localhost", 18322, pbftObj.getPublicKey())));
    CPre_prepare msg0 = pbftObj.assemblePre_prepare(64, "test");
    bool onRecvPre_prepare0 =  pbftObj.onReceivePrePrepare(msg0);
    BOOST_CHECK(onRecvPre_prepare0);
    CPre_prepare msg1 = pbftObj.assemblePre_prepare(64, "test1");
    bool onRecvPre_prepare1 =  pbftObj.onReceivePrePrepare(msg1);
    BOOST_CHECK(!onRecvPre_prepare1);
}


BOOST_AUTO_TEST_CASE(message_order){
    /* This test does not start udp server thread.
     */
    // If no failure occurs, 3 nodes are enough for normal operation.
    int port0 = 8350, port1 = 8342, port2 = 8343; 
    CPbft pbftObj0(port0, 0); // 18322 can be an arbitrary port because we do not start UDP server in this test.
    CPbft pbftObj1(port1, 100); 
    CPbft pbftObj2(port2, 200);
    pbftObj0.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    pbftObj0.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", port2, pbftObj2.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj2.server_id, CPbftPeer("localhost", port2, pbftObj2.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    pbftObj2.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    // pbftObj1 receives pre-prepare first; pbftObj2 receives prepare first.
    CPre_prepare pp = pbftObj0.assemblePre_prepare(64, "test");
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].prepareCount,  0);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].phase, PbftPhase::pre_prepare);
    pbftObj0.broadcast(&pp);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].phase, PbftPhase::prepare);
    
    // let pbftObj1 and pbftObj2 store pp in their log.
    pbftObj1.onReceivePrePrepare(pp);
    BOOST_CHECK_EQUAL(pbftObj1.log[pp.seq].prepareCount,  1);
    BOOST_CHECK_EQUAL(pbftObj1.log[pp.seq].phase, PbftPhase::prepare);
    CPbftMessage p1 = pbftObj1.assembleMsg(PbftPhase::prepare, pp.seq);
    pbftObj0.onReceivePrepare(p1,true);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].prepareCount,  1);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].phase, PbftPhase::prepare);
    pbftObj2.onReceivePrepare(p1,true);
    BOOST_CHECK_EQUAL(pbftObj2.log[pp.seq].prepareCount,  1);
    BOOST_CHECK_EQUAL(pbftObj2.log[pp.seq].phase, PbftPhase::pre_prepare);
    pbftObj2.onReceivePrePrepare(pp);
    BOOST_CHECK_EQUAL(pbftObj2.log[pp.seq].prepareCount,  2);
    BOOST_CHECK_EQUAL(pbftObj2.log[pp.seq].phase, PbftPhase::commit);
    CPbftMessage p2 = pbftObj2.assembleMsg(PbftPhase::prepare, pp.seq);
    pbftObj0.onReceivePrepare(p2, true);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].prepareCount,  2);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].phase, PbftPhase::commit);
    pbftObj1.onReceivePrepare(p2, true);
    BOOST_CHECK_EQUAL(pbftObj1.log[pp.seq].prepareCount,  2);
    BOOST_CHECK_EQUAL(pbftObj1.log[pp.seq].phase, PbftPhase::commit);
    
    // all three objects have collected 1 pre-prepare and 2 prepares, so they all have entered commit phase. 
    
}

BOOST_AUTO_TEST_CASE(udp_server){
    //This test case start a new thread to run udp server for each pbft object. Must use Ctrl-C to terminate this test.
    char pRecvBuf[CPbftMessage::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("127.0.0.1", clientUdpPort);
    
    // We cannot use a for loop to create CPbft objects and store them in an array or vector b/c no copy constructor is implemented. Even with pointers, objects created within a loop go out of scope once the control exits the loop and pointers become dangling.
    int port0 = 8350, port1 = 8342, port2 = 8343; 
    CPbft pbftObj0(port0, 0); 
    CPbft pbftObj1(port1, 1); 
    CPbft pbftObj2(port2, 2); 
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

void sendReq(std::string reqString, int port, UdpClient& pbftClient){
    std::ostringstream oss;
    oss << reqString; // do not put space here as space is used delimiter in stringstream.
    pbftClient.sendto(oss, "localhost", port);
}

BOOST_AUTO_TEST_SUITE_END()