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
#include "netmessagemaker.h"
#include <thread>
#include <chrono>
#include <time.h>
#include "txdb.h"
#include "init.h"
#include <float.h>

std::atomic<uint32_t> totalTxSent(0);
std::atomic<uint32_t> globalReqSentCnt(0);
bool sendingDone = false;
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

TxPlacer::TxPlacer():totalTxNum(0), alpha(0.5f), vecShardTxCount(num_committees, 0), loadScores(num_committees, 0), loadBalancingthld(50000000) {}


/* all output UTXOs of a tx is stored in one shard. */
std::vector<int32_t> TxPlacer::randomPlace(const CTransaction& tx) {
    std::vector<int32_t> ret;

    std::set<int> inputShardIds;
    /* add the input shard ids to the set */
    if (!tx.IsCoinBase()) { // do not calculate shard for dummy coinbase input.
	for(uint32_t i = 0; i < tx.vin.size(); i++) {
	    inputShardIds.insert(tx.vin[i].prevout.hash.GetCheapHash() % num_committees);
	}
    }

//    std::cout << "tx " << tx->GetHash().GetHex() << " spans shards : ";
//    for(auto entry : shardIds) {
//	std::cout << entry << " ";
//    }
//    std::cout << std::endl;

    /* add the output shard id to the above set */
    int32_t outShardId = tx.GetHash().GetCheapHash() % num_committees;
    if (inputShardIds.find(outShardId) != inputShardIds.end()) {
	/* inputShardIds.size() is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size()]++;
    } else {
	/* inputShardIds.size() + 1 is the shard span of this tx. */
	shardCntMap[tx.vin.size()][inputShardIds.size() + 1]++;
    }

    ret.resize(inputShardIds.size() + 1);
    ret[0] = outShardId;// put the outShardIt as the first element
    std::copy(inputShardIds.begin(), inputShardIds.end(), ret.begin() + 1);
    return ret;
}

int32_t TxPlacer::randomPlaceUTXO(const uint256& txid) {
    return txid.GetCheapHash() % num_committees;
}

CPlacementStatus::CPlacementStatus(): fitnessScore(num_committees, 0.0f), numOutTx(0), numUnspentCoin(0), placementRes(-1) { }

CPlacementStatus::CPlacementStatus(uint32_t num_upspent_coin): fitnessScore(num_committees, 0.0f), numOutTx(0), numUnspentCoin(num_upspent_coin), placementRes(-1) { }

std::vector<int32_t> TxPlacer::hashingPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
        }

        /* assign tx to the shard using hash value. */
        outputShard = randomPlaceUTXO(pTx->GetHash()); 

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    /* update  mapNotFullySpentTx after placement. */
    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    iter_bool_pair.first->second.placementRes = outputShard;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::optchainPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* --------Compute T2S score -----*/
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
        }

        /* Step 2: calculate p'(u) and the output shard. */
        std::vector<float> sumScore(num_committees, 0.0f);
        for (const uint256& txid: preReqTxs) {
            /* increment the number of out tx of this parent tx. */
            mapNotFullySpentTx[txid].numOutTx++;
            /* calculate the weighted fitness score sum */
            for (uint32_t i = 0; i < num_committees; i++) {
                const CPlacementStatus& parentStatus = mapNotFullySpentTx[txid];
                sumScore[i] += parentStatus.fitnessScore[i]/parentStatus.numOutTx;
            }
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
        //std::cout << "p(u) of the cur tx" << std::endl;
        float maxScore = 0;
        for (uint32_t i = 0; i < num_committees; i++) {
            float p_u_prime = (1 - alpha) * sumScore[i];
            placementStatus.fitnessScore[i] = p_u_prime;
            float p_u = p_u_prime / vecShardTxCount[i];
            //std::cout << __func__ << ": p(u) = " << p_u << std::endl;
            if (p_u > maxScore) {
               maxScore = p_u;
               outputShard = i; 
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update p'(u)  after placement. */
    iter_bool_pair.first->second.fitnessScore[outputShard] += alpha;
    iter_bool_pair.first->second.placementRes = outputShard;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    placementStatus.placementRes = outputShard;
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::mostInputUTXOPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
        }

        /* assign tx to the shard with the most number of input UTXO. */
        int mostInputUtxoInAShard = 0;
        for (auto const& p: mapInputShardUTXO) {
            if (p.second.size() > mostInputUtxoInAShard) {
               mostInputUtxoInAShard = p.second.size();
               outputShard = p.first; 
            }
        }

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update p'(u)  after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

class InputShardStat {
public:
    std::vector<uint32_t> utxoIndices;
    CAmount totalValue; // sum of all input UTXO's value 
    InputShardStat(): totalValue(0) { }
};

std::vector<int32_t> TxPlacer::mostInputValuePlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, InputShardStat> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            InputShardStat& inShardStat = mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes];
            inShardStat.utxoIndices.push_back(i);
            inShardStat.totalValue += mapNotFullySpentTx[parentTxid].txRef->vout[pTx->vin[i].prevout.n].nValue;
        }

        /* assign tx to the shard with the most input value. */
        CAmount maxValue = -1;
        for (auto const& p: mapInputShardUTXO) {
            if (p.second.totalValue > maxValue) {
               maxValue = p.second.totalValue;
               outputShard = p.first; 
            }
        }

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update placementRes after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;
    iter_bool_pair.first->second.txRef = pTx;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second.utxoIndices);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::firstUtxoPlace(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
            if (i == 0) {
                outputShard = mapNotFullySpentTx[parentTxid].placementRes; 
            }
        }


        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update placementRes after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;
    iter_bool_pair.first->second.txRef = pTx;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

/* assign tx to one of its input shard with hashing. */
std::vector<int32_t> TxPlacer::HPtoInputShard(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: Find the input UTXO to whose shard this tx will be assigned to.
          */
        uint inputUTXOIdx = pTx->GetHash().GetCheapHash() % pTx->vin.size();

        /* Step 2: find all parent tx, and assign the tx to the selected input shard. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
            if (i == inputUTXOIdx) {
                outputShard = mapNotFullySpentTx[parentTxid].placementRes; 
            }
        }

        /* clear fully spent parent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update placementRes after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;
    iter_bool_pair.first->second.txRef = pTx;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::HPOnlyCrossShardTx(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
        }

        if (mapInputShardUTXO.size() == 1) {
            /* this tx has only one input shard, place it to this shard. */
            outputShard = mapInputShardUTXO.begin()->first;
        } else {
            outputShard = randomPlaceUTXO(pTx->GetHash());
        }

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update placementRes after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;
    iter_bool_pair.first->second.txRef = pTx;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

static uint getMaxDifference(std::vector<uint>& vec, uint& min_val_index) {
    uint max_val = 0, min_val = UINT_MAX;
    for(uint i = 0; i < vec.size(); i++) {
        if (vec[i] > max_val) {
            max_val = vec[i]; 
        }
        if (vec[i] < min_val) {
            min_val = vec[i]; 
            min_val_index = i;
        }
    }
    return max_val - min_val;
}

std::vector<int32_t> TxPlacer::optchainPlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* --------Compute T2S score -----*/
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
        }

        /* Step 2: calculate p'(u) and the output shard. */
        std::vector<float> sumScore(num_committees, 0.0f);
        for (const uint256& txid: preReqTxs) {
            /* increment the number of out tx of this parent tx. */
            mapNotFullySpentTx[txid].numOutTx++;
            /* calculate the weighted fitness score sum */
            for (uint32_t i = 0; i < num_committees; i++) {
                const CPlacementStatus& parentStatus = mapNotFullySpentTx[txid];
                sumScore[i] += parentStatus.fitnessScore[i]/parentStatus.numOutTx;
            }
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }

        //std::cout << "p(u) of the cur tx" << std::endl;
        float maxScore = -FLT_MAX;
        uint latencySum = 0;
        for (uint32_t i = 0; i < num_committees; i++) {
            latencySum += g_pbft->expected_tx_latency[i].latency;
        }
        for (uint32_t i = 0; i < num_committees; i++) {
            float p_u_prime = (1 - alpha) * sumScore[i];
            placementStatus.fitnessScore[i] = p_u_prime;
            float p_u = p_u_prime / vecShardTxCount[i];
            /* calculate p(u) - 0.01 epsilon */
            //std::cout << __func__ << ": p(u) = " << p_u << ", expected latency = " << g_pbft->expected_tx_latency[i].latency << std::endl;
            /* normalize expected latency using num_committees and latencySum so that the latency score stays between 0~1 regradless of how long the expected latency is. */
            float overallScore = p_u - 0.01 * num_committees * g_pbft->expected_tx_latency[i].latency / latencySum;
            if (overallScore > maxScore) {
               maxScore = overallScore;
               outputShard = i; 
            }
        }
    }

    assert(outputShard >=0 && outputShard < num_committees);

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update p'(u)  after placement. */
    iter_bool_pair.first->second.fitnessScore[outputShard] += alpha;
    iter_bool_pair.first->second.placementRes = outputShard;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    placementStatus.placementRes = outputShard;
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::mostInputUTXOPlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
        }

        /* assign tx to the shard with the most number of input UTXO. */
        uint lowestScoreShard;
        if (getMaxDifference(loadScores, lowestScoreShard) < loadBalancingthld) {
            int mostInputUtxoInAShard = 0;
            for (auto const& p: mapInputShardUTXO) {
                if (p.second.size() > mostInputUtxoInAShard) {
                   mostInputUtxoInAShard = p.second.size();
                   outputShard = p.first; 
                }
            }
        } else {
            assert(lowestScoreShard >= 0 && lowestScoreShard < num_committees);
            outputShard = lowestScoreShard;
        }

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update p'(u)  after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::mostInputValuePlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, InputShardStat> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            InputShardStat& inShardStat = mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes];
            inShardStat.utxoIndices.push_back(i);
            inShardStat.totalValue += mapNotFullySpentTx[parentTxid].txRef->vout[pTx->vin[i].prevout.n].nValue;
        }

        /* assign tx to the shard with the most input value. */
        uint lowestScoreShard;
        if (getMaxDifference(loadScores, lowestScoreShard) < loadBalancingthld) {
            CAmount maxValue = -1;
            for (auto const& p: mapInputShardUTXO) {
                if (p.second.totalValue > maxValue) {
                   maxValue = p.second.totalValue;
                   outputShard = p.first; 
                }
            }
        } else {
            assert(lowestScoreShard >= 0 && lowestScoreShard < num_committees);
            outputShard = lowestScoreShard;
        }

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update placementRes after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;
    iter_bool_pair.first->second.txRef = pTx;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second.utxoIndices);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

std::vector<int32_t> TxPlacer::firstUtxoPlace_LB(const CTransactionRef pTx, std::deque<std::vector<uint32_t>>& vShardUtxoIdxToLock) {
    int32_t outputShard = -1;
    /* key is shard id, value is a vector of input utxos in this shard. */
    std::map<int32_t, std::vector<uint32_t>> mapInputShardUTXO;
    CPlacementStatus placementStatus(pTx->vout.size());
    if (pTx->IsCoinBase()) {
        outputShard = randomPlaceUTXO(pTx->GetHash());
    } else {
        /* Step 1: find all parent tx and set output shard to be the shard holding the first UTXO. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            /* decrement the remaining coin count of the parent tx */
            assert(mapNotFullySpentTx.find(parentTxid) != mapNotFullySpentTx.end());
            mapNotFullySpentTx[parentTxid].numUnspentCoin--;
            preReqTxs.emplace(parentTxid);
            mapInputShardUTXO[mapNotFullySpentTx[parentTxid].placementRes].push_back(i);
            if (i == 0) {
                outputShard = mapNotFullySpentTx[parentTxid].placementRes; 
            }
        }

        /* Step 2: check load balancing */
        uint lowestScoreShard;
        if (getMaxDifference(loadScores, lowestScoreShard) >= loadBalancingthld) {
            assert(lowestScoreShard >= 0 && lowestScoreShard < num_committees);
            outputShard = lowestScoreShard;
        }

        /* clear fully spent tx. */
        for (const uint256& txid: preReqTxs) {
            if (mapNotFullySpentTx[txid].numUnspentCoin == 0) {
                /* remove a tx b/c all its UTXOs has been spent. */
                mapNotFullySpentTx.erase(txid);
            }
        }
    }

    auto iter_bool_pair = mapNotFullySpentTx.emplace(pTx->GetHash(), std::move(placementStatus));
    /* update placementRes after placement. */
    iter_bool_pair.first->second.placementRes = outputShard;
    iter_bool_pair.first->second.txRef = pTx;

    /* increment tx count of the chosen shard. */
    vecShardTxCount[outputShard]++;
    
    /* prepare a resultant vector for return */
    std::vector<int32_t> ret;
    ret.reserve(mapInputShardUTXO.size() + 1);
    ret.push_back(outputShard); // put the outShardId as the first element
    for (auto it = mapInputShardUTXO.begin(); it != mapInputShardUTXO.end(); it++) {
        ret.push_back(it->first);
        vShardUtxoIdxToLock.push_back(it->second);
    }
    assert(vShardUtxoIdxToLock.size() + 1 == ret.size());
    return ret;
}

void TxPlacer::updateLoadScore(uint shard_id, ClientReqType reqType, uint nSigs) {
    switch(reqType) {
        case ClientReqType::TX:
            loadScores[shard_id] += 150 * nSigs + 35;
            break;
        case ClientReqType::LOCK:
            loadScores[shard_id] += 170 * nSigs + 192;
            break;
        case ClientReqType::UNLOCK_TO_COMMIT:
            loadScores[shard_id] += 125 * nSigs + 5;
            break;
        default:
            std::cout << __func__ << "invalid client req type " << std::endl;
    }
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

static void delayByNoop(const int noop_count) {
    int k = 0;
    uint oprand = noop_count;

    for (; k < noop_count; k++) {
	if (ShutdownRequested())
	    return;
	oprand ^= k;
    }
    std::cerr << "loop noop for " << k << " times. oprand becomes " << oprand << std::endl;
}

static uint32_t sendTxChunk(const CBlock& block, const uint start_height, const uint block_height, const uint32_t start_tx, const int noop_count, std::vector<std::deque<std::shared_ptr<CClientReq>>>& batchBuffers, uint32_t& reqSentCnt, TxPlacer& txPlacer, const uint placementMethod) {
    uint32_t cnt = 0;
    CPbft& pbft = *g_pbft;
    uint end_tx = std::min(start_tx + txChunkSize, block.vtx.size());
    for (uint j = start_tx; j < end_tx; j++) {
        const CTransaction& tx = *block.vtx[j]; 
        /* find all pending parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        pbft.lock_tx_delayed.lock();
        for (uint32_t i = 0; i < tx.vin.size(); i++) {
            const uint256& parentTxid = tx.vin[i].prevout.hash;
            if (pbft.mapTxDelayed.find(parentTxid) != pbft.mapTxDelayed.end()) {
                preReqTxs.emplace(parentTxid);
            }
        }
        pbft.lock_tx_delayed.unlock();

        if (preReqTxs.empty()) {
            /* has no pending parent tx, send this tx. */
            sendTx(block.vtx[j], j, block_height, batchBuffers, reqSentCnt, txPlacer, placementMethod);
            cnt++;
            /* delay by doing noop. */
            delayByNoop(noop_count);
        } else {
            /* has pending parent tx, add the tx to dependency graph. */
            TxIndexOnChain txIdx(block_height, j);
            pbft.lock_tx_delayed.lock();
            /* add tx to dependency graph as a child tx. */
            for (const uint256& parent_tx : preReqTxs) {
                if (pbft.mapTxDelayed.find(parent_tx) != pbft.mapTxDelayed.end()) {
                    pbft.mapTxDelayed[parent_tx].childTxns.push_back(txIdx);
                    //std::cout << "added tx " << tx.GetHash().ToString() << " as a child tx of " << parent_tx.ToString() << std::endl;
                }
            }
            pbft.mapRemainingPrereq[txIdx] = preReqTxs.size();
            //std::cout << "tx " << tx.GetHash().ToString() << " has " << pbft.mapRemainingPrereq[txIdx] << " pending parent tx."  << std::endl;
            /* add tx to dependency graph as a potential parent tx. */
            pbft.mapTxDelayed.emplace(tx.GetHash(), TxBlockInfo(block.vtx[j], block_height, j));
            pbft.lock_tx_delayed.unlock();
             //std::cout << "delay tx " << tx.GetHash().ToString() << ", pending parent tx number = " << preReqTxs.size() << std::endl;
        }
    }
    return cnt;
}

static uint32_t sendQueuedTx(const int startBlock, const int noop_count, std::vector<std::deque<std::shared_ptr<CClientReq>>>& batchBuffers, uint32_t& reqSentCnt, TxPlacer& txPlacer, const uint placementMethod) {
    int txSentCnt = 0;
    CPbft& pbft = *g_pbft;
    if (pbft.commitSentTxns.empty())
        return txSentCnt; 
    
    /* add all child tx to a queue that will be used by BFS. */
    std::queue<TxIndexOnChain> q;
    if (pbft.lock_commit_sent_txns.try_lock()) {
        
        for(const CTransactionRef pTx: pbft.commitSentTxns) {
            /* add child tx to queue. */
            assert(pbft.mapTxDelayed.find(pTx->GetHash()) != pbft.mapTxDelayed.end());
            //std::cout << "removing tx " << pTx->GetHash().ToString() << " from delay map. It has child tx cnt = " << pbft.mapTxDelayed[pTx->GetHash()].childTxns.size() << std::endl;
            for (const TxIndexOnChain& dependent: pbft.mapTxDelayed[pTx->GetHash()].childTxns) {

                assert(pbft.mapRemainingPrereq.find(dependent) != pbft.mapRemainingPrereq.end());
                //std::cout << "dependent tx " <<  dependent.ToString() << " has pending parenet tx cnt " << pbft.mapRemainingPrereq[dependent]  << std::endl;
                if (--pbft.mapRemainingPrereq[dependent] == 0) {
                    q.push(dependent);
                    pbft.mapRemainingPrereq.erase(dependent);
                }
            }
            /* for COMMIT requests, a server must verify (f + 1) signatures from every
             * input shards. 
             */
            txPlacer.updateLoadScore(pbft.mapTxDelayed[pTx->GetHash()].outputShard, ClientReqType::TX, (pbft.nFaulty + 1) * pbft.inputShardReplyMap[pTx->GetHash()].lockReply.size()) ;
            pbft.lock_tx_delayed.lock();
            pbft.mapTxDelayed.erase(pTx->GetHash());
            pbft.lock_tx_delayed.unlock();
        }
        pbft.commitSentTxns.clear();
        pbft.lock_commit_sent_txns.unlock();
    }
    /* bfs. */
    while (!q.empty()) {
        const TxIndexOnChain& txIdx(q.front());
        const uint256& txid = pbft.blocks2Send[txIdx.block_height - startBlock].vtx[txIdx.offset_in_block]->GetHash();
        bool isSingleShard = sendTx(pbft.blocks2Send[txIdx.block_height - startBlock].vtx[txIdx.offset_in_block], txIdx.offset_in_block, txIdx.block_height, batchBuffers, reqSentCnt, txPlacer, placementMethod);
        txSentCnt++;
        delayByNoop(noop_count);
        if (isSingleShard) {
            /* add child tx to queue. Only child of single-shard tx can be sent b/c child of cross-shard tx will be sent after the parent tx's COMMIT req is sent. */
            const TxBlockInfo& txBlkInfo = pbft.mapTxDelayed[txid];
            for (const TxIndexOnChain& dependent : txBlkInfo.childTxns) {
                assert(pbft.mapRemainingPrereq.find(dependent) != pbft.mapRemainingPrereq.end());
                if (--pbft.mapRemainingPrereq[dependent] == 0) {
                    q.push(dependent);
                    pbft.mapRemainingPrereq.erase(dependent);
                }
            }
            //std::cout << "erasing single shard tx from delay map, tx =  " << txid.ToString() << std::endl;
            g_pbft->lock_tx_delayed.lock();
            g_pbft->mapTxDelayed.erase(txid);
            g_pbft->lock_tx_delayed.unlock();
        }
        q.pop();
    }
    
    //std::cout << __func__ << " sent " <<  txSentCnt << " queued tx. queue size = "  << listDelaySendingTx.size() << std::endl;
    return txSentCnt;
}

void TxPlacer::printTxSendRes() {
    uint maxTxCnt = 0, minTxCnt = UINT_MAX; 
    //std::cout << "tx cnt in each shard : ";
    for (int i = 0; i < num_committees; i++) {
        uint txCntInShard = vecShardTxCount[i];
        //std::cout << i << " = " << txCntInShard << ", ";
        if (txCntInShard > maxTxCnt) {
            maxTxCnt = txCntInShard;
        }
        if (txCntInShard < minTxCnt) {
            minTxCnt = txCntInShard;
        }
    }

    uint maxScore = 0, minScore = UINT_MAX; 
    //std::cout << "tx cnt in each shard : ";
    for (int i = 0; i < num_committees; i++) {
        uint load_score = loadScores[i];
        //std::cout << i << " = " << txCntInShard << ", ";
        if (load_score > maxScore) {
            maxScore = load_score;
        }
        if (load_score < minScore) {
            minScore = load_score;
        }
    }
    std::cout << "shard MAX tx cnt = " << maxTxCnt << ", shard MIN tx cnt = " << minTxCnt << ", difference = " << maxTxCnt- minTxCnt << ", MAX load score = " << maxScore << ", MIN load score = " << minScore << ", score diff = " << maxScore - minScore << std::endl;
}

static void probeShardLatency() {
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    for (uint i = 0; i < num_committees; i++) {
        gettimeofday(&(g_pbft->expected_tx_latency[i].probe_send_time), NULL);
        g_connman->PushMessage(g_pbft->leaders[i], msgMaker.Make(NetMsgType::LATENCY_PROBE));
        //std::cout << "send probe to shard " << i << std::endl;
    }
}

void sendTxOfThread(const int startBlock, const int endBlock, const uint32_t thread_idx, const uint32_t num_threads, const int noop_count, const uint placementMethod) {
    RenameThread(("sendTx" + std::to_string(thread_idx)).c_str());
    uint32_t cnt = 0, reqSentCnt = 0;
    const uint32_t jump_length = num_threads * txChunkSize;
    std::vector<std::deque<std::shared_ptr<CClientReq>>> batchBuffers(num_committees);
    TxPlacer txPlacer;
    CPbft& pbft = *g_pbft;
    struct timeval start_time, end_time;
    struct timeval start_time_all_block, end_time_all_block;
    gettimeofday(&start_time_all_block, NULL);
    for (int block_height = startBlock; block_height < endBlock; block_height++) {
        if (ShutdownRequested())
            break;
        if (placementMethod == 5) {
        /* probe shard leaders to get communication latency and verification latency */
            probeShardLatency();
        }
        CBlock& block = g_pbft->blocks2Send[block_height - startBlock];
        for (size_t i = thread_idx * txChunkSize; i < block.vtx.size(); i += jump_length){
            //std::cout << __func__ << ": thread " << thread_idx << " sending No." << i << " tx in block " << block_height << std::endl;
            gettimeofday(&start_time, NULL);
            uint32_t actual_chunk_size = sendTxChunk(block, startBlock, block_height, i, noop_count, batchBuffers, reqSentCnt, txPlacer, placementMethod);
            gettimeofday(&end_time, NULL);
            cnt += actual_chunk_size;
            //std::cout << __func__ << ": thread " << thread_idx << " sent " << actual_chunk_size << " tx in block " << block_height << ". The sending takes " << (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_usec - start_time.tv_usec) << " us." << std::endl;

            /* check delay sending tx after sending a chunk.  */
            gettimeofday(&start_time, NULL);
            uint32_t cnt_queued_tx_sent = sendQueuedTx(startBlock, noop_count, batchBuffers, reqSentCnt, txPlacer, placementMethod);
            cnt += cnt_queued_tx_sent;
            gettimeofday(&end_time, NULL);
            //std::cout << "sent " <<  cnt_queued_tx_sent << " tx queued. The sending takes " << (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_usec - start_time.tv_usec) << " us." << std::endl;
        }
    }

    std::cout << end_time.tv_sec << "." << end_time.tv_usec << ", chunky sending ends.\n";
    /* calculate the total number of tx in all the blocks*/
    uint32_t nAllTx = 0;
    for (int block_height = startBlock; block_height < endBlock; block_height++) {
        nAllTx += g_pbft->blocks2Send[block_height - startBlock].vtx.size();
    }
    /* send remaing tx. For time measurement only */
    //std::cout << "remaining tx cnt to send = " << nAllTx - cnt << std::endl;
    //while (cnt < nAllTx || !g_pbft->mapTxDelayed.empty()) {
    //    if (ShutdownRequested())
    //        break;
    //    cnt += sendQueuedTx(startBlock, noop_count, batchBuffers, reqSentCnt, txPlacer, placementMethod);

    //    /* add all req in our local batch buffer to the global batch buffer. */
    //    for (uint i = 0; i < batchBuffers.size(); i++) {
    //        std::deque<std::shared_ptr<CClientReq>>& shardBatchBuffer = batchBuffers[i];
    //        if (!shardBatchBuffer.empty()) {
    //            pbft.add2BatchOnlyBuffered(i, shardBatchBuffer);
    //        }
    //    }

    //    usleep(200);
    //}

    gettimeofday(&end_time_all_block, NULL);
    std::cout << __func__ << ": thread " << thread_idx << " sent " << cnt << " tx in total. All tx of this thread takes " << (end_time_all_block.tv_sec - start_time_all_block.tv_sec)*1000000 + (end_time_all_block.tv_usec - start_time_all_block.tv_usec) << " us. Totally sentReqCnt = " << reqSentCnt << ". Block " << startBlock << "~" << endBlock << " have tx cnt = " << nAllTx << ", noop cnt = " << noop_count << std::endl;
    txPlacer.printTxSendRes();
    totalTxSent += cnt; 
    globalReqSentCnt += reqSentCnt;
}

/* return true if the tx is single-shard, otherwise, return false. */
bool sendTx(const CTransactionRef tx, const uint idx, const uint32_t block_height, std::vector<std::deque<std::shared_ptr<CClientReq>>>& batchBuffers, uint32_t& reqSentCnt, TxPlacer& txPlacer, const uint placementMethod) {
	/* get the input shards and output shards id*/
    std::deque<std::vector<uint32_t>> vShardUtxoIdxToLock;
	std::vector<int32_t> shards;
    bool isSingleShard = false;
        switch (placementMethod) {
            case 0: 
                shards = txPlacer.optchainPlace(tx, vShardUtxoIdxToLock);
                break;
            case 1:
                shards = txPlacer.mostInputUTXOPlace(tx, vShardUtxoIdxToLock);
                break;
            case 2:
                shards = txPlacer.mostInputValuePlace(tx, vShardUtxoIdxToLock);
                break;
            case 3:
                shards = txPlacer.firstUtxoPlace(tx, vShardUtxoIdxToLock);
                break;
            case 4:
                shards = txPlacer.HPtoInputShard(tx, vShardUtxoIdxToLock);
                break;
            case 5:
                shards = txPlacer.HPOnlyCrossShardTx(tx, vShardUtxoIdxToLock);
                break;
            case 6:
                shards = txPlacer.hashingPlace(tx, vShardUtxoIdxToLock);
                break;
            case 7: 
                shards = txPlacer.optchainPlace_LB(tx, vShardUtxoIdxToLock);
                break;
            case 8:
                shards = txPlacer.mostInputUTXOPlace_LB(tx, vShardUtxoIdxToLock);
                break;
            case 9:
                shards = txPlacer.mostInputValuePlace_LB(tx, vShardUtxoIdxToLock);
                break;
            case 10:
                shards = txPlacer.firstUtxoPlace_LB(tx, vShardUtxoIdxToLock);
                break;
            default:
                std::cout << "invalid placement method." << std::endl;
                return false;
        }
	const uint256& hashTx = tx->GetHash();

	assert((tx->IsCoinBase() && shards.size() == 1) || (!tx->IsCoinBase() && shards.size() >= 2)); // there must be at least one output shard and one input shard for non-coinbase tx.
	//std::cout << idx << "-th" << " tx "  <<  hashTx.GetHex().substr(0, 10) << " : ";
	//for (int shard : shards)
	//    std::cout << shard << ", ";
	//std::cout << std::endl;

	if ((shards.size() == 2 && shards[0] == shards[1]) || shards.size() == 1) {
	    /* this is a single shard tx */
        isSingleShard = true;
	    g_pbft->add2Batch(shards[0], ClientReqType::TX, tx, batchBuffers[shards[0]]);
        reqSentCnt++;
	    if (shards.size() != 1) {
                /* only count non-coinbase tx b/c coinbase tx do not 
                 * have the time-consuming sig verification step. */
                txPlacer.updateLoadScore(shards[0], ClientReqType::TX, tx->vin.size());
	    }
        //std::cout << "send TX req for tx " << hashTx.ToString() << " to shard " << shards[0] << std::endl;
	} else {
	    /* this is a cross-shard tx */
        /* add this tx to delayed sending map b/c its COMMIT req is delayed to when all input shards respond. */
        if (g_pbft->mapTxDelayed.find(hashTx) == g_pbft->mapTxDelayed.end()) {
            g_pbft->lock_tx_delayed.lock();
            g_pbft->mapTxDelayed.insert(std::make_pair(hashTx, std::move(TxBlockInfo(tx, block_height, idx, shards[0]))));
            g_pbft->lock_tx_delayed.unlock();
            //std::cout << "add CROSS-SHARD tx " << hashTx.ToString() << " to delayed map" << std::endl;
        } else {
            /* This tx has been added to the delay map b/c has pending parent tx and has never been set a valid output shard, so we should update the output shard. */
            g_pbft->mapTxDelayed[hashTx].outputShard = shards[0];
            //std::cout << "updated CROSS-SHARD tx " << hashTx.ToString() << " in delayed map" << std::endl;
        }
        /* send LOCK req */
	    for (uint i = 1; i < shards.size(); i++) {
                g_pbft->inputShardReplyMap[hashTx].lockReply.emplace(shards[i], std::vector<CInputShardReply>());
                g_pbft->inputShardReplyMap[hashTx].decision.store('\0', std::memory_order_relaxed);
                g_pbft->add2Batch(shards[i], ClientReqType::LOCK, tx, batchBuffers[shards[i]], &vShardUtxoIdxToLock[i - 1]);
                reqSentCnt++;
                txPlacer.updateLoadScore(shards[i], ClientReqType::LOCK, vShardUtxoIdxToLock[i - 1].size());
                //std::cout << "send LOCK req for tx " << hashTx.ToString() << " to shard " << shards[i] << std::endl;
	    }
	}
	return isSingleShard;
}

TxIndexOnChain::TxIndexOnChain(): block_height(0), offset_in_block(0) { }

TxIndexOnChain::TxIndexOnChain(uint32_t block_height_in, uint32_t offset_in_block_in):
 block_height(block_height_in), offset_in_block(offset_in_block_in) { }

bool TxIndexOnChain::IsNull() {
    return block_height == 0 && offset_in_block == 0;
}

TxIndexOnChain TxIndexOnChain::operator+(const unsigned int oprand) {
    const unsigned int cur_block_size = chainActive[block_height]->nTx;
    if (offset_in_block + oprand < cur_block_size) {
	return TxIndexOnChain(block_height, offset_in_block + oprand);
    } else {
	uint32_t cur_block = block_height + 1;
	uint32_t cur_oprand = oprand - (cur_block_size - offset_in_block);
	while (cur_oprand >= chainActive[cur_block]->nTx) {
	    cur_oprand -= chainActive[cur_block]->nTx;
	    cur_block++;
	}
	return TxIndexOnChain(cur_block, cur_oprand);
    }
}

bool operator<(const TxIndexOnChain& a, const TxIndexOnChain& b)
{
    return a.block_height < b.block_height || 
	    (a.block_height == b.block_height && a.offset_in_block < b.offset_in_block);
}

bool operator>(const TxIndexOnChain& a, const TxIndexOnChain& b)
{
    return a.block_height > b.block_height || 
	    (a.block_height == b.block_height && a.offset_in_block > b.offset_in_block);
}

bool operator==(const TxIndexOnChain& a, const TxIndexOnChain& b) {
    return a.block_height == b.block_height && a.offset_in_block == b.offset_in_block;
}

bool operator!=(const TxIndexOnChain& a, const TxIndexOnChain& b) {
    return ! (a == b);
}

bool operator<=(const TxIndexOnChain& a, const TxIndexOnChain& b) {
    return a.block_height < b.block_height || 
	    (a.block_height == b.block_height && a.offset_in_block <= b.offset_in_block);
}

std::string TxIndexOnChain::ToString() const {
    return "(" + std::to_string(block_height) + ", " + std::to_string(offset_in_block) + ")";
}

DependencyRecord::DependencyRecord(): tx(), prereq_tx() { }
DependencyRecord::DependencyRecord(const uint32_t block_height, const uint32_t offset_in_block, const TxIndexOnChain& latest_prereq_tx_in): tx(block_height, offset_in_block), prereq_tx(latest_prereq_tx_in) { }

uint TxPlacer::countInputTx(const CTransactionRef pTx) {
        /* find all parent tx. */
        std::unordered_set<uint256, uint256Hasher> preReqTxs;
        for (uint32_t i = 0; i < pTx->vin.size(); i++) {
            const uint256& parentTxid = pTx->vin[i].prevout.hash;
            preReqTxs.emplace(parentTxid);
        }
        return preReqTxs.size();
}