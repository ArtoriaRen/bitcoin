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

extern bool pessimistic;
extern uint32_t CHUNK_SIZE;

class SnapshotMetadata{
public:
    uint256 snapshotHash; // the hash of metadata fields concatenated with the merekle root of chunk hash. 
    int currentChainLength; // the chain length used when a new peer request this snapshot. Used by optimist peer to detect longest chain switch.

    /* snapshot block info */
    CBlockHeader blockHeader; // snapshot block header
    int height; // snapshot block height
    unsigned int timeMax; // Maximum nTime in the chain up to and including this block.
    uint256 chainWork; // Total amount of work (expected number of hashes) in the chain up to and including this block
    uint32_t nTimeLastDifficultyAdjustmentBlock; // used to calculate future PoW difficulty.
    unsigned int chainTx; //cannot be obtained from a header chain.

    /* chunk hashes */
    std::vector<uint256> vChunkHash;
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(snapshotHash);
        READWRITE(currentChainLength);
        READWRITE(blockHeader);
        READWRITE(height);
        READWRITE(timeMax);
        READWRITE(chainWork);
        READWRITE(nTimeLastDifficultyAdjustmentBlock);
        READWRITE(chainTx);
        READWRITE(vChunkHash);
    }

    uint256 getSnapshotBlockInfoHash() const;
};

class Snapshot{
private:
    std::vector<COutPoint> unspent;
    std::vector<COutPoint> added;
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> spent;
    std::vector<COutPoint> vOutpoint;
//    std::deque<CBlockHeader> headerChain;
    uint32_t period;
public:

    /* restrict the number of coins transferred in a SNAPSHOT message to 100k
     * because message size in Bitcoin currently has an uplimit of 4MB, and we
     * believe 100k coins will not exceed the limit.
     */
    SnapshotMetadata snpMetadata;
    uint32_t receivedChunkCnt; //count how many chunks having been received so far. Used to determine when snapshot sync ends.
    mutable uint32_t nReceivedUTXOCnt; //for stat printing

    /* for pessimistic sync only. Do not download block if we have not downloaded
     * the latest snapshot yet. */
    bool notYetDownloadSnapshot;

    /* time stat in ms */
    unsigned long snpDownloadTime;
    unsigned long applyTime;

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
    uint256 takeSnapshot();
    
    /* the original updateCoin plus update the unspent and spent sets. */
    void updateCoins(const CCoinsMap& mapCoins);

    /* add UTXOs in the chunk to the chainstate database. */
    void applyChunk(CDataStream& vRecv) const;

//    void appendToHeaderChain(const std::vector<CBlockHeader>& headers);
    /* TODO: the merkle tree should have metadata hash as a leaf. */
    bool verifyMetadata(const SnapshotMetadata& metadata, const uint256& snpHashOnChain) const;
    bool verifyChunk(const uint32_t chunkId, CDataStream& vRecv) const;
    int getLastSnapshotBlockHeight() const;
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

bool headerEqual(const CBlockHeader& lhs, const CBlockHeader& rhs);

extern std::unique_ptr<Snapshot> psnapshot;


#endif /* SNAPSHOT_H */

