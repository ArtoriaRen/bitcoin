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
#include "txdb.h"
#include "init.h"

static const uint32_t SEC = 1000000; // 1 sec = 10^6 microsecond

uint32_t num_committees;
int lastAssignedAffinity = -1;
/* txChunkSize * numThreads should be less than block size. Otherwise, some thread
 * has nothing to do. We should consertively set the num of threads less than 20 
 * given the txChunkSize of 100.
 */
size_t txChunkSize = 100; 
//uint32_t txStartBlock;
//uint32_t txEndBlock;

TxPlacer::TxPlacer():totalTxNum(0){}


/* all output UTXOs of a tx is stored in one shard. */
std::vector<int32_t> TxPlacer::randomPlace(const CTransaction& tx, const CCoinsViewCache& cache){
    std::vector<int32_t> ret;

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

    ret.resize(inputShardIds.size() + 1);
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

void sendTxOfThread(const std::vector<CBlock>& vBlocksToSend, int startBlock, uint32_t thread_idx, uint32_t num_threads, int txSendPeriod, std::promise<int>&& count) {
    uint32_t cnt;
    const uint32_t jump_length = num_threads * txChunkSize;
    for (size_t j = 0; j < vBlocksToSend.size(); j++) {
	const CBlock& block = vBlocksToSend[j];
	uint block_height = startBlock + j;
	for (size_t i = thread_idx * txChunkSize; i < block.vtx.size(); i += jump_length){
	    std::cout << __func__ << ": thread " << thread_idx << " sending No." << i << " tx in block " << block_height << std::endl;
	    uint32_t actual_chunk_size = sendTxChunk(block, block_height, i, txSendPeriod);
	    cnt += actual_chunk_size;
	    std::cout << __func__ << ": thread " << thread_idx << " sent " << actual_chunk_size << " tx in block " << block_height << std::endl;
	}
    }
    std::cout << __func__ << ": thread " << thread_idx << " sent " << cnt << " tx in total" << std::endl;
    count.set_value(cnt);
}

uint32_t sendTxChunk(const CBlock& block, const uint block_height, const uint32_t start_tx, int txSendPeriod) {
    const struct timespec sleep_length = {0, txSendPeriod};
    uint32_t cnt = 0;
    uint32_t end_tx = std::min(start_tx + txChunkSize, block.vtx.size());
    for (uint j = start_tx; j < end_tx; j++) {
	sendTx(block.vtx[j], j, block_height);
	cnt++;
	//nanosleep(&sleep_length, NULL);

	/* send one aborted tx every four tx */
	if ((j & 0x04) == 0) {
	    if (ShutdownRequested())
	    	return cnt;
	    //while (!g_pbft->txResendQueue.empty()) {
	    //    TxBlockInfo& txInfo = g_pbft->txResendQueue.front();
	    //    sendTx(txInfo.tx, txInfo.n, txInfo.blockHeight);
	    //    g_pbft->txResendQueue.pop_front();
	    //    cnt++;
	    //    //nanosleep(&sleep_length, NULL);
	    //}
	}
    }

    return cnt;
}

uint32_t sendAllTailTx(int txSendPeriod) {
    /* We have sent all tx but those waiting for prerequisite tx. Poll the 
     * queue to see if some dependent tx are ready until we sent all tx. */
    std::cout << "sending " << g_pbft->txResendQueue.size() << " tail tx ... " << std::endl;
    const struct timespec sleep_length = {0, txSendPeriod * 1000};
    uint32_t cnt = 0;
    while (!g_pbft->txResendQueue.empty()) {
	TxBlockInfo& txInfo = g_pbft->txResendQueue.front();
	sendTx(txInfo.tx, txInfo.n, txInfo.blockHeight);
	g_pbft->txResendQueue.pop_front();
	cnt++;
	/* still sleep for a while to give depended tx enough time to finish. */
	nanosleep(&sleep_length, NULL);
	if (ShutdownRequested())
		break;
    }
    return cnt;
}

bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height) {
	/* get the input shards and output shards id*/
	//TxPlacer txPlacer;
	//CCoinsViewCache view(pcoinsTip.get());
	//std::vector<int32_t> shards = txPlacer.randomPlace(*tx, view);
	const uint256& hashTx = tx->GetHash();

	const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
	//assert((tx->IsCoinBase() && shards.size() == 1) || (!tx->IsCoinBase() && shards.size() >= 2)); // there must be at least one output shard and one input shard for non-coinbase tx.
	//std::cout << idx << "-th" << " tx "  <<  hashTx.GetHex().substr(0, 10) << " : ";
	//std::cout << idx << "-th" << " tx "  <<  hashTx.GetHex().substr(0, 10) << std::endl;
	//for (int shard : shards)
	//    std::cout << shard << ", ";
	//std::cout << std::endl;

	/* send tx and collect time info to calculate latency. 
	 * We also remove all reply msg for this req for resending aborted tx. */
	//g_pbft->replyMap[hashTx].clear();
	//g_pbft->mapTxStartTime.erase(hashTx);
	g_pbft->txInFly.insert(std::make_pair(hashTx, std::move(TxBlockInfo(tx, block_height, idx))));
	struct TxStat stat;
	//if ((shards.size() == 2 && shards[0] == shards[1]) || shards.size() == 1) {
	    /* this is a single shard tx */
	    stat.type = TxType::SINGLE_SHARD;
	    gettimeofday(&stat.startTime, NULL);
	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
	    //g_connman->PushMessage(g_pbft->leaders[shards[0]], msgMaker.Make(NetMsgType::PBFT_TX, *tx));
	    g_connman->PushMessage(g_pbft->leaders[0], msgMaker.Make(NetMsgType::PBFT_TX, *tx));
//	} else {
//	    /* this is a cross-shard tx */
//	    stat.type = TxType::CROSS_SHARD;
//	    gettimeofday(&stat.startTime, NULL);
//	    g_pbft->mapTxStartTime.insert(std::make_pair(hashTx, stat));
//	    for (uint i = 1; i < shards.size(); i++) {
//		g_pbft->inputShardReplyMap[hashTx].lockReply.insert(std::make_pair(shards[i], std::vector<CInputShardReply>()));
//		g_pbft->inputShardReplyMap[hashTx].decision = '\0';
//		g_connman->PushMessage(g_pbft->leaders[shards[i]], msgMaker.Make(NetMsgType::OMNI_LOCK, *tx));
//	    }
//	}
	g_pbft->nTotalSentTx++;
	return true;
}
