/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include <boost/thread.hpp>
#include <snapshot/snapshot.h>
#include <validation.h>
#include <consensus/merkle.h>
#include <txdb.h>
#include <hash.h>
#include <serialize.h>
#include <vector>
#include <fstream>

#include "chainparams.h"

extern BlockMap& mapBlockIndex;

bool pessimistic = false;
uint32_t CHUNK_SIZE = 50000; // limit the chunks msg size below 4M msg limit.

uint256 SnapshotMetadata::getSnapshotBlockInfoHash() const {
    CHash256 hasher; 	
    uint256 hash;
    hasher.Write((const unsigned char*)&blockHeader.nVersion, sizeof(blockHeader.nVersion))
	    .Write(blockHeader.hashPrevBlock.begin(), blockHeader.hashPrevBlock.size())
	    .Write(blockHeader.hashMerkleRoot.begin(), blockHeader.hashMerkleRoot.size())
	    .Write((const unsigned char*)&blockHeader.nTime, sizeof(blockHeader.nTime))
	    .Write((const unsigned char*)&blockHeader.nBits, sizeof(blockHeader.nBits))
	    .Write((const unsigned char*)&blockHeader.nNonce, sizeof(blockHeader.nNonce))
	    .Write((const unsigned char*)&height, sizeof(height))
	    .Write((const unsigned char*)&timeMax, sizeof(timeMax))
	    .Write(chainWork.begin(), chainWork.size())
	    .Write((const unsigned char*)&nTimeLastDifficultyAdjustmentBlock, sizeof(nTimeLastDifficultyAdjustmentBlock))
	    .Write((const unsigned char*)&nTimeLastTenBlocks, (nMedianTimeSpan - 1) * sizeof(nTimeLastTenBlocks[0]))
	    .Write((const unsigned char*)&chainTx, sizeof(chainTx));
    hasher.Finalize((unsigned char*)&hash);
    return hash;
}

Snapshot::Snapshot(): period(1000), receivedChunkCnt(0), nReceivedUTXOCnt(0), notYetDownloadSnapshot(true), snpDownloadTime(0), applyTime(0) {
    unspent.reserve(pcoinsTip->GetCacheSize());
}

void Snapshot::initialLoad() {
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
    assert(pcursor);
    
    // iterate chain state database
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        if (pcursor->GetKey(key)) {
            unspent.emplace(std::move(key));
        } else {
            std::cout << __func__ << ": unable to read key" << std::endl;
        }
        pcursor->Next();
    }
}

void Snapshot::sendChunk(CNode* pfrom, const CNetMsgMaker& msgMaker, CConnman* connman, uint32_t chunkId) const {
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    uint32_t startIdx = chunkId * CHUNK_SIZE;
    uint64_t stopIdx = vOutpoint.size() < (chunkId + 1) * CHUNK_SIZE ? vOutpoint.size() : (chunkId + 1) * CHUNK_SIZE;
    for (uint32_t i = startIdx; i < stopIdx; i++){
        auto it = spent.find(vOutpoint[i]); 
        if ( it != spent.end()){
            /* The coin is in the spent set. */
            it->first.Serialize(ds);
            it->second.Serialize(ds);
            } else {
                /* Have to fetch the coin from the current coins view. */
                Coin coin;
                bool found = pcoinsTip->GetCoin(vOutpoint[i], coin);
                /* unspent coins must exist */
                assert(found);
            vOutpoint[i].Serialize(ds);
            coin.Serialize(ds);
        }
    }

    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::CHUNK, chunkId, ds));
    LogPrint(BCLog::NET, "send chunk: startIdx=%d, endIdx=%d, total coin num =%d.\n", startIdx, stopIdx - 1, vOutpoint.size());
}

//void Snapshot::appendToHeaderChain(const std::vector<CBlockHeader>& headers) {
//    headerChain.insert(headerChain.end(), headers.begin(), headers.end());
//}

bool Snapshot::verifyMetadata(const SnapshotMetadata& metadata, const uint256& snpHashOnChain) const {
    uint256 chunkMerkleRoot = ComputeMerkleRoot(metadata.vChunkHash, NULL);   
    uint256 snapshotBlockInfoHash = metadata.getSnapshotBlockInfoHash();
    CHash256 hasher; 	
    uint256 hash;
    hasher.Write(chunkMerkleRoot.begin(), chunkMerkleRoot.size())
	    .Write(snapshotBlockInfoHash.begin(), snapshotBlockInfoHash.size());
    hasher.Finalize((unsigned char*)&hash);
    return hash == snpHashOnChain; 
}

bool Snapshot::verifyChunk(const uint32_t chunkId, CDataStream& vRecv) const {
    CHash256 hasher; 	
    uint256 hash;
    hasher.Write((const unsigned char*)vRecv.data(), vRecv.size());
    hasher.Finalize((unsigned char*)&hash);
    return hash == snpMetadata.vChunkHash[chunkId]; 
}

int Snapshot::getLastSnapshotBlockHeight() const{
    //return snpMetadata.currentChainLength - snpMetadata.currentChainLength % period;
    /* in our test, we specify the snapshot block height from conf file */
    return snpMetadata.height;
}

uint256 Snapshot::takeSnapshot() {
    /* Should use lock to stop other threads reading snaphshot, but in current test,
     * we only get snapshot after we know one has been generate.*/
    unspent.insert(added.begin(), added.end());
    added.clear();
    spent.clear();
    /* keep a copy of the outpoints we used to create the snapshot, so that when
     * we sends the snapshot to a new peer in the future, we can easily cut a 
     * snapshot into chunks because we know all the order of the outpoints.
     */
    vOutpoint = std::deque<COutPoint>(unspent.begin(), unspent.end());
    /* only sort the unspent set when we are creating a snapshot, not when we are
     * verifying a snapshot receivd from an old peer, because an peers send us 
     * outpoints in sorted order using snapshot chunks.
     */
    std::sort(vOutpoint.begin(), vOutpoint.end());
    std::vector<uint256> leaves;
    uint num_chunks = (unspent.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    leaves.reserve(num_chunks);
    for (uint i = 0; i < num_chunks; i++) {
	CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
	CHash256 hasher; 	
	uint256 hash;
	uint end = (i == num_chunks -1) ? unspent.size() : (i + 1) * CHUNK_SIZE;
	/* hash the concatenation of all coins in a chunk. */
	for (uint j = i * CHUNK_SIZE; j < end; j++) {
	    COutPoint key = vOutpoint[j];
	    Coin coin;
	    bool found = pcoinsTip->GetCoin(key, coin);
	    /* unspent coins must exist */
	    assert(found);
	    key.Serialize(ds);
	    coin.Serialize(ds);
	}
	hasher.Write((const unsigned char*)ds.data(), ds.size())
		.Finalize((unsigned char*)&hash);
	leaves.push_back(hash);
    }
    /* note down which block encloses the snapshot. We only do this if we are an
     * old peer and is producing snapshot. For new peer using this function 
     * calculating snapshot merkle root, there is no need to update.*/
    if (chainActive.Tip()) {
        snpMetadata.blockHeader = chainActive.Tip()->GetBlockHeader();
        snpMetadata.height = chainActive.Height();
        snpMetadata.timeMax = chainActive.Tip()->nTimeMax;
        snpMetadata.chainWork = ArithToUint256(chainActive.Tip()->nChainWork);
	uint32_t lastDifficultyAdjustmentBlockHeight = snpMetadata.height - snpMetadata.height % Params().GetConsensus().DifficultyAdjustmentInterval();
//	std::cout << "last difficulty adjustment height = " << lastDifficultyAdjustmentBlockHeight << std::endl;
        snpMetadata.nTimeLastDifficultyAdjustmentBlock = chainActive[lastDifficultyAdjustmentBlockHeight ]->nTime;
        const CBlockIndex* pindex = chainActive.Tip();
        pindex = pindex->pprev; // start with the predecessor of the snapshot block
        for (int i = nMedianTimeSpan - 2 ; i > 0 ; i--, pindex = pindex->pprev) {
            snpMetadata.nTimeLastTenBlocks[i] = pindex->nTime;
        }
        
        snpMetadata.chainTx = chainActive.Tip()->nChainTx;
        uint256 chunkMerkleRoot = ComputeMerkleRoot(leaves, NULL);   
        uint256 snapshotBlockInfoHash = snpMetadata.getSnapshotBlockInfoHash();
        CHash256 hasher; 	
        hasher.Write(chunkMerkleRoot.begin(), chunkMerkleRoot.size())
            .Write(snapshotBlockInfoHash.begin(), snapshotBlockInfoHash.size());
        hasher.Finalize((unsigned char*)&snpMetadata.snapshotHash);
        snpMetadata.vChunkHash.swap(leaves);
    } 
    return snpMetadata.snapshotHash;
}

void Snapshot::spendCoin(const COutPoint& outpoint, const Coin& coin) {
    auto iter = unspent.find(outpoint);  
    if (iter != unspent.end()) {
        spent.emplace(std::move(*iter), coin);
        unspent.erase(iter);
    } else {
        added.erase(outpoint);
    }
}

void Snapshot::addCoins(const CTransaction& tx) {
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        added.emplace(tx.GetHash(), i);
    }
}

void Snapshot::applyChunk(CDataStream& vRecv) const {
    /* Unserialize UTXOs and add them to the chainstate database. */
    while (!vRecv.eof()) {
        COutPoint key;
        Coin coin;
        vRecv >> key;
        vRecv >> coin;
        pcoinsTip->AddCoin(std::move(key), std::move(coin), false);
        nReceivedUTXOCnt++;
    }
}

bool Snapshot::IsNewPeerWithValidSnapshot() const {
    uint256 snapshotBlockHash = snpMetadata.blockHeader.GetHash();
    return mapBlockIndex.find(snapshotBlockHash) != mapBlockIndex.end() &&
		mapBlockIndex[snapshotBlockHash]->pprev == nullptr;
}

int64_t Snapshot::GetMedianTimePast(const CBlockIndex* pindexStart) const
{
    int64_t pmedian[nMedianTimeSpan];
    int64_t* pbegin = &pmedian[nMedianTimeSpan];
    int64_t* pend = &pmedian[nMedianTimeSpan];

    int nAvailableIndex = pindexStart->nHeight - snpMetadata.height + 1;
    const CBlockIndex* pindex = pindexStart;
    for (int i = 0; i < nAvailableIndex; i++, pindex = pindex->pprev) {
        *(--pbegin) = pindex->GetBlockTime();
//        std::cout << " pindex->height = " << pindex->nHeight << std::endl;
    }
    for (int i = 0; i < nMedianTimeSpan - nAvailableIndex; i++) {
        *(--pbegin) = snpMetadata.nTimeLastTenBlocks[nMedianTimeSpan - 2 - i];
//        std::cout << " index in nTimeLastTenBlocks  = " << nMedianTimeSpan - 2 - i << std::endl;
    }
//    std::cout << "distance between pbegin and pend = " << pend - pbegin << std::endl;
    std::sort(pbegin, pend);
    return pbegin[(pend - pbegin)/2];
}

std::string Snapshot::ToString() const
{
    std::string ret;
    uint256 snapshotBlockHash = snpMetadata.blockHeader.GetHash();
    if (!snapshotBlockHash.IsNull()) {
		ret.append("snapshot block = ");
		ret.append(snapshotBlockHash.GetHex());
		ret.append("\nheight = ");
		ret.append(std::to_string(snpMetadata.height));
		ret.append("\n");
    } 

    ret.append("lastsnapshotmerkleroot = ");
    ret.append(snpMetadata.snapshotHash.GetHex());

    ret.append("\nunspent.size() = ");
    ret.append(std::to_string(unspent.size()));
    ret.append("\nunspent = ");
    for(auto& outpoint: unspent) {
        ret.append(outpoint.ToString());
        ret.append(", ");
    }

    ret.append("\nspent.size() = ");
    ret.append(std::to_string(spent.size()));
    ret.append("\nspent = ");
    for (auto it = spent.begin(); it != spent.end(); it++) {
        ret.append(it->first.ToString());
        ret.append(", ");
    }

    ret.append("\nadded.size() = ");
    ret.append(std::to_string(added.size()));
    ret.append("\nadded = ");
    for(auto& outpoint: added) {
        ret.append(outpoint.ToString());
        ret.append(", ");
    }
    return ret;
}

void Snapshot::Write2File() const
{
    std::ofstream file;
    file.open("snapshot.out");
    snpMetadata.Serialize(file);
    std::cout << __func__ << ": unspent vector size = " <<  unspent.size() << std::endl;
    file << unspent.size();
    for (const auto& outpoint: unspent) {
        outpoint.Serialize(file);
    }
    std::cout << __func__ << ": spent map size = " <<  spent.size() << std::endl;
    file << spent.size();
    for (const auto& pair: spent) {
        pair.first.Serialize(file);
        pair.second.Serialize(file);
    }
}

bool headerEqual(const CBlockHeader& lhs, const CBlockHeader& rhs) {
    return lhs.nVersion == rhs.nVersion && lhs.hashPrevBlock == rhs.hashPrevBlock 
	    && lhs.hashMerkleRoot == rhs.hashMerkleRoot && lhs.nTime == rhs.nTime
	    && lhs.nBits == rhs.nBits && lhs.nNonce == rhs.nNonce;
}

std::unique_ptr<Snapshot> psnapshot;
