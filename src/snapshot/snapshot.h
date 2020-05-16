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

extern uint32_t MAX_COIN_NUM_PER_MSG;

class OutpointCoinPair{
public:
    COutPoint op;
    Coin coin;

    // empty constructor
    OutpointCoinPair();

    OutpointCoinPair(COutPoint opIn, Coin coinIn);

    template<typename Stream>
    void Serialize(Stream &s) const {
	op.Serialize(s);
	coin.Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream &s) {
	op.Unserialize(s);
	coin.Unserialize(s);
    }
};

class BlockHeaderAndHeight{
public:
    CBlockHeader header;
    int height;
    uint256 snapshotMerkleRoot;
    // Maximum nTime in the chain up to and including this block.
    unsigned int timeMax;
    // Total amount of work (expected number of hashes) in the chain up to and including this block
    uint256 chainWork;
    unsigned int chainTx;
    unsigned int numChunks;
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(height);
        READWRITE(snapshotMerkleRoot);
        READWRITE(timeMax);
        READWRITE(chainWork);
        READWRITE(chainTx);
        READWRITE(numChunks);
    }
};

class Snapshot{
private:
    std::vector<COutPoint> unspent;
    std::vector<COutPoint> added;
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> spent;
public:
    /* restrict the number of coins transferred in a SNAPSHOT message to 100k
     * because message size in Bitcoin currently has an uplimit of 4MB, and we
     * believe 100k coins will not exceed the limit.
     */
    BlockHeaderAndHeight headerNheight;
    uint256 snapshotBlockHash;
    uint256 lastSnapshotMerkleRoot;
    uint32_t frequency; // in blocks
    mutable uint32_t chunkCnt; // count how many snapshot chunks have been sent or received.
    std::vector<COutPoint> snapshotOutpointArray;

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
    // std::vector<OutpointCoinPair> getSnapshot(int idx) const;
    void sendSnapshot(CNode* pfrom, const CNetMsgMaker& msgMaker, CConnman* connman) const;
    uint256 takeSnapshot(bool updateBlkInfo = true);
    void updateCoins(const CCoinsMap& mapCoins);
//    void spendCoin(const COutPoint& op);
    void receiveSnapshot(CDataStream& vRecv);

    bool verifySnapshot();
    
    /* determine if we are a new peer with a valid snapshot base on if pprev 
     * of the snapshot block index is nullptr. 
     */
    bool IsNewPeerWithValidSnapshot() const;

    inline size_t getSnapshotSize(){
	return unspent.size();
    }

    std::string ToString() const;

//    inline bool valid(){
//	return !lastSnapshotMerkleRoot.IsNull();
//    }

    /* serialization */
};

extern std::unique_ptr<Snapshot> psnapshot;


#endif /* SNAPSHOT_H */

