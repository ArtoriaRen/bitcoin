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

//class OutpointCoinPair{
//public:
//    COutPoint op;
//    Coin coin;
//    template<typename Stream>
//    void Serialize(Stream &s) const {
//	op.Serialize(s);
//	coin.Serialize(s);
//    }
//    template<typename Stream>
//    void Unserialize(Stream &s) {
//	op.Unserialize(s);
//	coin.Unserialize(s);
//    }
//};

class BlockHeaderAndHeight{
public:
    CBlockHeader header;
    int height;
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(height);
    }
};

class Snapshot{
private:
    std::vector<COutPoint> unspent;
    std::vector<COutPoint> added;
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> spent;
public:
    CBlockIndex blkinfo;
    uint256 lastSnapshotMerkleRoot;
    uint32_t frequency; // in blocks

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
    std::vector<COutPoint> getSnapshot() const;
    uint256 takeSnapshot();
    void updateCoins(const CCoinsMap& mapCoins);
//    void spendCoin(const COutPoint& op);
    void receiveSnapshot(CDataStream& vRecv);
    
    std::string ToString() const;

    /* serialization */
};

extern std::unique_ptr<Snapshot> psnapshot;


#endif /* SNAPSHOT_H */

