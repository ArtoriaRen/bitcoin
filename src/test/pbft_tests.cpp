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
   CPbft pbftObj(18322); // 18322 can be an arbitrary port because we do not start UDP server in this test.
   CPbftMessage msg0 = pbftObj.assemblePre_prepare(64, "test");
   BOOST_CHECK(pbftObj.checkMsg(msg0));
   CPbftMessage msg1 = pbftObj.assemblePre_prepare(64, "test1");
   BOOST_CHECK(!pbftObj.checkMsg(msg1));

}

BOOST_AUTO_TEST_SUITE_END()