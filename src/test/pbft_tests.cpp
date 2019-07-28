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

BOOST_AUTO_TEST_CASE(conflict_digest)
{
    // a server should not be able to accept conflicting pre-prepare
    CPbft pbftObj(18322, 0); // 18322 can be an arbitrary port because we do not start UDP server in this test.
    CPre_prepare msg0 = pbftObj.assemblePre_prepare(64, "test");
    pbftObj.broadcast(&msg0);
    BOOST_CHECK(pbftObj.checkMsg(msg0));
    CPre_prepare msg1 = pbftObj.assemblePre_prepare(64, "test1");
    BOOST_CHECK(!pbftObj.checkMsg(msg1));
    
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
    CPbftMessage pp = pbftObj0.assemblePre_prepare(64, "test");
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
    int port0 = 8350, port1 = 8342; 
    char pRecvBuf[CPbftMessage::messageSizeBytes]; // buf to receive msg from pbft servers.
    int clientUdpPort = 18500; // the port of udp server at the pbft client side.
    UdpServer udpServer("localhost", clientUdpPort);
    
    CPbft pbftObj0(port0, 0); 
    CPbft pbftObj1(port1, 1); 
    std::cout << "peer 0 pk = " << pbftObj0.getPublicKey().GetHash().ToString() << std::endl;
    std::cout << "peer 1 pk = " << pbftObj1.getPublicKey().GetHash().ToString() << std::endl;
    pbftObj0.peers.insert(std::make_pair(pbftObj1.server_id, CPbftPeer("localhost", port1, pbftObj1.getPublicKey())));
    pbftObj1.peers.insert(std::make_pair(pbftObj0.server_id, CPbftPeer("localhost", port0, pbftObj0.getPublicKey())));
    std::thread t0(interruptableReceive, std::ref(pbftObj0));
    std::thread t1(interruptableReceive, std::ref(pbftObj1));
    
    // To emulate a pbft client, we use a udp client to send request to the pbft leader.
    UdpClient pbftClient;
    std::string reqString = "r x=8"; // the format of a request is r followed by the real request

    std::ostringstream oss;
    oss << reqString; // do not put space here as space is used delimiter in stringstream.
    pbftClient.sendto(oss, "localhost", port0);
    ssize_t recvBytes =  udpServer.recv(pRecvBuf, CPbftMessage::messageSizeBytes);
    std::string recv(pRecvBuf, 0, recvBytes);
    BOOST_CHECK_EQUAL(recv, reqString.substr(2));
    t0.join();
    t1.join();
}


BOOST_AUTO_TEST_SUITE_END()