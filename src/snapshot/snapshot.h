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
    uint256 snapshotHash; // the hash of metadata fields concatenated with the merekle root of chunk hash. 

    /* header */
    int height; // snapshot block height
    uint256 snapshotBlockHash;
    unsigned int nChunks; // number of chunks in the snapshot.
    unsigned int chainTx; //cannot be obtained from a header chain.

    /* chunk hashes */
    std::vector<uint256> vChunkHash;
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(snapshotHash);
        READWRITE(height);
        READWRITE(snapshotBlockHash);
        READWRITE(nChunks);
        READWRITE(chainTx);
        READWRITE(vChunkHash);
    }

    uint256 getHeaderHash() const;
};

class Snapshot{
public:
    SnapshotMetadata snpMetadata;
    std::vector<std::string> vSerializedChunks; /* serialized chunks. */

    uint32_t receivedChunkCnt; //count how many chunks having been received so far. Used to determine when snapshot sync ends.
    mutable uint32_t nReceivedUTXOCnt; //for stat printing

    /* Do not download block if we have not apply the latest snapshot yet. */
    bool notYetDownloadSnapshot;

    Snapshot();

    void sendChunk(CNode* pfrom, const CNetMsgMaker& msgMaker, CConnman* connman, uint32_t chunkId) const;
    /* create a snapshot at the current block height. */
    uint256 takeSnapshot();
    
    /* the original updateCoin plus update the unspent and spent sets. */
    void updateCoins(const CCoinsMap& mapCoins);

    /* add UTXOs in the chunk to the chainstate database. */
    void applySnapshot() const;

//    void appendToHeaderChain(const std::vector<CBlockHeader>& headers);
    /* TODO: the merkle tree should have metadata hash as a leaf. */
    bool verifyMetadata(const SnapshotMetadata& metadata) const;
    bool verifyChunk(const uint32_t chunkId, const std::string& chunk) const;

    std::string ToString() const;
    void Write2File() const;
};

bool headerEqual(const CBlockHeader& lhs, const CBlockHeader& rhs);

extern std::unique_ptr<Snapshot> psnapshot;


#endif /* SNAPSHOT_H */

