/*
 * Created by Liuyang Ren on July 16 2019.
 */
#include <util.h>
#include <test/test_bitcoin.h>

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

#include "pbft/pbft.h"
#include "pbft/pbft_log_entry.h"
#include "pbft/pbft_msg.h"
#include "pbft/udp_server_client.h"

BOOST_FIXTURE_TEST_SUITE(pbft_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(conflict_digest)
{
    // a server should not be able to accept conflicting pre-prepare
    CPbft pbftObj(18322); // 18322 can be an arbitrary port because we do not start UDP server in this test.
    CPbftMessage msg0 = pbftObj.assemblePre_prepare(64, "test");
    BOOST_CHECK(pbftObj.checkMsg(msg0));
    CPbftMessage msg1 = pbftObj.assemblePre_prepare(64, "test1");
    BOOST_CHECK(!pbftObj.checkMsg(msg1));
    
}


BOOST_AUTO_TEST_CASE(message_order){
    /* This test does not start udp server thread.
     */
    // If no failure occurs, 3 nodes are enough for normal operation.
    CPbft pbftObj0(18322, 0); // 18322 can be an arbitrary port because we do not start UDP server in this test.
    CPbft pbftObj1(18323, 100); 
    CPbft pbftObj2(18324, 200);
    pbftObj0.peerPubKeys.insert(std::make_pair(pbftObj1.server_id, pbftObj1.getPublicKey()));
    pbftObj0.peerPubKeys.insert(std::make_pair(pbftObj2.server_id, pbftObj2.getPublicKey()));
    pbftObj1.peerPubKeys.insert(std::make_pair(pbftObj0.server_id, pbftObj0.getPublicKey()));
    pbftObj1.peerPubKeys.insert(std::make_pair(pbftObj2.server_id, pbftObj2.getPublicKey()));
    pbftObj2.peerPubKeys.insert(std::make_pair(pbftObj0.server_id, pbftObj0.getPublicKey()));
    pbftObj2.peerPubKeys.insert(std::make_pair(pbftObj1.server_id, pbftObj1.getPublicKey()));
    // pbftObj1 receives pre-prepare first; pbftObj2 receives prepare first.
    CPbftMessage pp = pbftObj0.assemblePre_prepare(64, "test");
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].prepareCount,  0);
    BOOST_CHECK_EQUAL(pbftObj0.log[pp.seq].phase, PbftPhase::pre_prepare);
    pbftObj0.broadcast(pp);
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

BOOST_AUTO_TEST_SUITE_END()