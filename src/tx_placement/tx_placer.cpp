/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <tx_placement/tx_placer.h>

#include "hash.h"
#include "arith_uint256.h"
#include "chain.h"
#include "validation.h"
#include "chainparams.h"
#include "netmessagemaker.h"
#include "pbft/pbft.h"
#include <thread>
#include <chrono>
#include <time.h>

static const uint32_t SEC = 1000000; // 1 sec = 10^6 microsecond

uint32_t num_committees;
int lastAssignedAffinity = -1;
//uint32_t txStartBlock;
//uint32_t txEndBlock;

TxPlacer::TxPlacer():totalTxNum(0){}


/* all output UTXOs of a tx is stored in one shard. */
std::vector<int32_t> TxPlacer::randomPlace(const CTransaction& tx){
	    std::set<int> inputShardIds;

    /* add the input shard ids to the set */
    if (!tx.IsCoinBase()) { // do not calculate shard for dummy coinbase input.
	for(uint32_t i = 0; i < tx.vin.size(); i++) {
	    arith_uint256 txid = UintToArith256(tx.vin[i].prevout.hash);
	    arith_uint256 quotient = txid / num_committees;
	    arith_uint256 inShardId = txid - quotient * num_committees;
	    inputShardIds.insert((int)(inShardId.GetLow64()));
	}
    }

//    std::cout << "tx " << tx->GetHash().GetHex() << " spans shards : ";
//    for(auto entry : shardIds) {
//	std::cout << entry << " ";
//    }
//    std::cout << std::endl;

    /* add the output shard id to the above set */
    arith_uint256 txid = UintToArith256(tx.GetHash());
    arith_uint256 quotient = txid / num_committees;
    arith_uint256 outShardId = txid - quotient * num_committees;
    if (inputShardIds.find((int)(outShardId.GetLow64())) != inputShardIds.end()) {
	/* inputShardIds.size() is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size()]++;
    } else {
	/* inputShardIds.size() + 1 is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size() + 1]++;
    }

    std::vector<int32_t> ret(inputShardIds.size() + 1);
    ret[0] = (int32_t)(outShardId.GetLow64());// put the outShardIt as the first element
    std::copy(inputShardIds.begin(), inputShardIds.end(), ret.begin() + 1);
    return ret;
}

int32_t TxPlacer::randomPlaceUTXO(const uint256& txid) {
	arith_uint256 txid_arth = UintToArith256(txid);
	arith_uint256 quotient = txid_arth / num_committees;
	arith_uint256 inShardId = txid_arth - quotient * num_committees;
	return (int32_t)(inShardId.GetLow64());
}

int32_t TxPlacer::smartPlace(const CTransaction& tx, CCoinsViewCache& cache){
	return -1;
}

void TxPlacer::printPlaceResult(){
    std::cout << "total tx num = " << totalTxNum << std::endl;
    std::cout << "tx shard num stats : " << std::endl;
    for(auto entry: shardCntMap) {
	std::cout << "\t" <<  entry.first << "-input_UTXO tx: " << std::endl;
	for (auto p : entry.second) {
	    std::cout << "\t\t" << p.first << "-shard tx count = " << p.second << std::endl;
	}
    }
}

//uint32_t sendTxInBlock(uint32_t block_height, struct timeval& expected_last_send_time, int txSendPeriod) {
uint32_t sendTxInBlock(uint32_t block_height, int txSendPeriod) {
    CBlock block;
    CBlockIndex* pblockindex = chainActive[block_height];
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
        std::cerr << "Block not found on disk" << std::endl;
    }
    std::cout << __func__ << "sending " << block.vtx.size() << " tx in block " << block_height << std::endl;
    uint32_t cnt = 0;
    for (uint j = 0; j < block.vtx.size(); j++) {
	CTransactionRef tx = block.vtx[j];
	const uint256& hashTx = tx->GetHash();
	/* get the input shards and output shards id*/
	TxPlacer txPlacer;
	std::vector<int32_t> shards = txPlacer.randomPlace(*tx);
	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	assert((tx->IsCoinBase() && shards.size() == 1) || (!tx->IsCoinBase() && shards.size() >= 2)); // there must be at least one output shard and one input shard for non-coinbase tx.
	std::cout << "tx "  <<  hashTx.GetHex().substr(0, 10) << " : ";
	for (int shard : shards)
	    std::cout << shard << ", ";
	std::cout << std::endl;

	/*-----calculate how long we should sleep------*/
//	struct timeval current;
//	struct timespec sleep_length = {0, 0};
//	if (expected_last_send_time.tv_usec + txSendPeriod > SEC){
//	    /* we assume that (expected_last_send_time.tv_usec + txSendPeriod) < 2 SEC */
//	    expected_last_send_time.tv_sec++;
//	    expected_last_send_time.tv_usec = expected_last_send_time.tv_usec + txSendPeriod - SEC;
//	} else {
//	    expected_last_send_time.tv_usec += txSendPeriod;
//	}
//	/* now expected_last_send_time becomes the time at which we should send this tx. */
//	gettimeofday(&current, NULL);
//	if (current.tv_sec < expected_last_send_time.tv_sec || (current.tv_sec == expected_last_send_time.tv_sec && current.tv_usec < expected_last_send_time.tv_usec)) {
//	    /* we are not reach the expected sending time yet, so sleep for a while.
//	     * we assume that this thread should never sleep more than 1 second. */
//	    sleep_length.tv_nsec = (expected_last_send_time.tv_sec - current.tv_sec) * SEC * 1000
//		    + (expected_last_send_time.tv_usec - current.tv_usec) * 1000;
//	    nanosleep((const timespec*)&sleep_length, NULL);
//	    std::cout << __func__ << ": slept " << sleep_length.tv_sec << "s " 
//		    << sleep_length.tv_nsec << "ns" << std::endl;
//	} else {
//	    std::cout << __func__ << ": do not sleep" << std::endl;
//	}


	/* send tx and collect time info to calculate latency. 
	 * We also remove all reply msg for this req to ease testing with sending a req multi times. */
	g_pbft->replyMap[hashTx].clear();
	g_pbft->mapTxStartTime.erase(hashTx);
	struct TxStat stat;
	if ((shards.size() == 2 && shards[0] == shards[1]) || shards.size() == 1) {
	    /* this is a single shard tx */
	    stat.type = TxType::SINGLE_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    g_connman->PushMessage(g_pbft->leaders[shards[0]], msgMaker.Make(NetMsgType::PBFT_TX, *tx));
	} else {
	    /* this is a cross-shard tx */
	    stat.type = TxType::CROSS_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    for (uint i = 1; i < shards.size(); i++) {
		g_pbft->inputShardReplyMap[hashTx].lockReply.insert(std::make_pair(shards[i], std::vector<CInputShardReply>()));
		g_pbft->inputShardReplyMap[hashTx].decision = '\0';
		g_connman->PushMessage(g_pbft->leaders[shards[i]], msgMaker.Make(NetMsgType::OMNI_LOCK, *tx));
	    }
	}
	g_pbft->mapTxid.insert(std::make_pair(hashTx, tx));
	cnt++;
	const struct timespec sleep_length = {0, txSendPeriod * 1000};
	nanosleep(&sleep_length, NULL);
    }
    return cnt;
}
