/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   snapshot.h
 * Author: l27ren
 *
 * Created on April 29, 2020, 11:30 PM
 */

#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <unordered_map>
#include <coins.h>
#include <net_processing.h>
#include <chain.h>
#include <netmessagemaker.h>

extern uint32_t CHUNK_SIZE;

class OutpointCoinPair{
public:
    COutPoint outpoint;
    Coin coin;

    // empty constructor
    OutpointCoinPair();

    OutpointCoinPair(COutPoint opIn, Coin coinIn);

    template<typename Stream>
    void Serialize(Stream &s) const {
	outpoint.Serialize(s);
	coin.Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream &s) {
	outpoint.Unserialize(s);
	coin.Unserialize(s);
    }
};

class SnapshotMetadata{
public:
    CBlockHeader blockHeader;
    int height;
    int currentChainLength;
    uint256 merkleRoot;

    /* TODO: check if the following field are not available on header chain. If not, can they be calculated using the header chain? or stored also in the coinbase field (check the coinbase field size)*/
    // Maximum nTime in the chain up to and including this block.
    unsigned int timeMax;
    // Total amount of work (expected number of hashes) in the chain up to and including this block
    uint256 chainWork;
    unsigned int chainTx;
    std::vector<uint256> vChunkHash;
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHeader);
        READWRITE(height);
        READWRITE(currentChainLength);
        READWRITE(merkleRoot);
        READWRITE(timeMax);
        READWRITE(chainWork);
        READWRITE(chainTx);
        READWRITE(vChunkHash);
    }
};

class Snapshot{
private:
    std::vector<COutPoint> unspent;
    std::vector<COutPoint> added;
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> spent;
    std::vector<COutPoint> vOutpoint;
public:
    /* restrict the number of coins transferred in a SNAPSHOT message to 100k
     * because message size in Bitcoin currently has an uplimit of 4MB, and we
     * believe 100k coins will not exceed the limit.
     */
    SnapshotMetadata snpMetadata;
    uint32_t receivedChunkCnt; //count how many chunks having been received so far. Used to determine when snapshot sync ends.
    mutable uint32_t nReceivedUTXOCnt; //for stat printing

    Snapshot();

    /* feed unspent vector with all outpoints in chainstate database.
     * Strictly speaking, all the unspent, added, spent vectors to disk
     * because otherwise we lose which coins are newly added since the 
     * last snapshot, especially permantly lose the those coins spent 
     * since the last snapshot. However, we only do in-memory test in
     * our prototype: a snapshot is build after booting up. And this 
     * snapshot becomes the latest snapshot, so the added and spent
     * vector are expected to be empty.
     */
    void initialLoad();
    void sendChunk(CNode* pfrom, const CNetMsgMaker& msgMaker, CConnman* connman, uint32_t chunkId) const;
    /* create a snapshot at the current block height. */
    uint256 takeSnapshot(bool updateBlkInfo = true);
    
    /* the original updateCoin plus update the unspent and spent sets. */
    void updateCoins(const CCoinsMap& mapCoins);

    /* add UTXOs in the chunk to the chainstate database. */
    void acceptChunk(std::vector<OutpointCoinPair>& chunk) const;

    bool verifyChunkHashes(const uint256& snpHashOnChain) const;
    bool verifyChunk(const uint32_t chunkId, const std::vector<OutpointCoinPair>& chunk) const;
    /* determine if we are a new peer with a valid snapshot base on if pprev 
     * of the snapshot block index is nullptr. 
     */
    bool IsNewPeerWithValidSnapshot() const;

    std::string ToString() const;
    void Write2File() const;

//    inline bool valid(){
//	return !lastSnapshotMerkleRoot.IsNull();
//    }

    /* serialization */
};

extern std::unique_ptr<Snapshot> psnapshot;


#endif /* SNAPSHOT_H */

