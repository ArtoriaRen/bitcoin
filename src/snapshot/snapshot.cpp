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

extern BlockMap& mapBlockIndex;

bool pessimistic = false;
uint32_t CHUNK_SIZE = 50000; // limit the chunks msg size below 4M msg limit.

OutpointCoinPair::OutpointCoinPair(){ }

OutpointCoinPair::OutpointCoinPair(COutPoint opIn, Coin coinIn): outpoint(opIn), coin(coinIn){ }

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
	    .Write((const unsigned char*)&chainTx, sizeof(chainTx));
    hasher.Finalize((unsigned char*)&hash);
    return hash;
}

Snapshot::Snapshot(): period(1000), receivedChunkCnt(0), nReceivedUTXOCnt(0), notYetDownloadSnapshot(true) {
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
            unspent.push_back(std::move(key));
        } else {
            std::cout << __func__ << ": unable to read key" << std::endl;
        }
        pcursor->Next();
    }
}

void Snapshot::sendChunk(CNode* pfrom, const CNetMsgMaker& msgMaker, CConnman* connman, uint32_t chunkId) const {
    std::vector<OutpointCoinPair> chunk;
    uint32_t startIdx = chunkId * CHUNK_SIZE;
    uint64_t stopIdx = vOutpoint.size() < (chunkId + 1) * CHUNK_SIZE ? vOutpoint.size() : (chunkId + 1) * CHUNK_SIZE;
    chunk.reserve(stopIdx - startIdx);
    for (uint32_t i = startIdx; i < stopIdx; i++){
        auto it = spent.find(vOutpoint[i]); 
        if ( it != spent.end()){
            /* The coin is in the spent set. */
            chunk.emplace_back(it->first, it->second);
        } else {
            /* Have to fetch the coin from the current coins view. */
            Coin coin;
            bool found = pcoinsTip->GetCoin(vOutpoint[i], coin);
            /* unspent coins must exist */
            assert(found);
            //OutpointCoinPair coinPair(snapshotOutpointArray[i], std::move(coin));
            chunk.emplace_back(vOutpoint[i], std::move(coin));
        }
    }

    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::CHUNK, chunkId, chunk));
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

bool Snapshot::verifyChunk(const uint32_t chunkId, const std::vector<OutpointCoinPair>& chunk) const {
    CHash256 hasher; 	
    uint256 hash;
    std::stringstream ss;
    for (const OutpointCoinPair& pair: chunk) {
	ss.str("");
	pair.outpoint.Serialize(ss);
	std::string key_str(ss.str());
	ss.str("");
	pair.coin.Serialize(ss);
	std::string coin_str(ss.str());
	hasher.Write((const unsigned char*)key_str.c_str(), sizeof(key_str.size()))
		.Write((const unsigned char*)coin_str.c_str(), sizeof(coin_str.size()));
    }
    hasher.Finalize((unsigned char*)&hash);
    return hash == snpMetadata.vChunkHash[chunkId]; 
}

int Snapshot::getLastSnapshotBlockHeight() const{
    return snpMetadata.currentChainLength - snpMetadata.currentChainLength % period;
}

uint256 Snapshot::takeSnapshot() {
    /* Should use lock to stop other threads reading snaphshot, but in current test,
     * we only get snapshot after we know one has been generate.*/
    unspent.insert(unspent.end(), added.begin(), added.end());
    added.clear();
    spent.clear();
    /* only sort the unspent set when we are creating a snapshot, not when we are
     * verifying a snapshot receivd from an old peer, because an peers send us 
     * outpoints in sorted order using snapshot chunks.
     */
    std::sort(unspent.begin(), unspent.end());
    /* keep a copy of the outpoints we used to create the snapshot, so that when
     * we sends the snapshot to a new peer in the future, we can easily cut a 
     * snapshot into chunks because we know all the order of the outpoints.
     */
    vOutpoint = unspent; 
    std::vector<uint256> leaves;
    uint num_chunks = (unspent.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    leaves.reserve(num_chunks);
    for (uint i = 0; i < num_chunks; i++) {
	std::stringstream ss;
    //CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
	CHash256 hasher; 	
	uint256 hash;
	uint end = (i == num_chunks -1) ? unspent.size() : (i + 1) * CHUNK_SIZE;
	/* hash the concatenation of all coins in a chunk. */
	for (uint j = i * CHUNK_SIZE; j < end; j++) {
	    COutPoint key = unspent[j];
	    Coin coin;
	    bool found = pcoinsTip->GetCoin(key, coin);
	    /* unspent coins must exist */
	    assert(found);

	    ss.str("");
	    key.Serialize(ss);
	    std::string key_str(ss.str());
	    ss.str("");
	    coin.Serialize(ss);
	    std::string coin_str(ss.str());
	    hasher.Write((const unsigned char*)key_str.c_str(), sizeof(key_str.size()))
		    .Write((const unsigned char*)coin_str.c_str(), sizeof(coin_str.size()));
	}
	hasher.Finalize((unsigned char*)&hash);
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

void Snapshot::updateCoins(const CCoinsMap& mapCoins){
    for (CCoinsMap::const_iterator it = mapCoins.begin(); it != mapCoins.end(); it++) {
        // Ignore non-dirty entries.
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            continue;
        }

	/* dirty, fresh and spent. No need to add it to the added set. */
	/* why is it dirty? */
        if (it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent()) {
	    continue;
	}
	
	/* dirty, fresh, but not spent yet */
	if (it->second.flags & CCoinsCacheEntry::FRESH) {
	    added.emplace_back(it->first.hash, it->first.n);
	    continue;
	}

	bool isCoinInLastSnapshot = false;
        std::vector<COutPoint>::iterator itUs = std::find(unspent.begin(), unspent.end(), it->first);
	if (itUs != unspent.end()){
	    isCoinInLastSnapshot = true;
	} else {
	    itUs = std::find(added.begin(), added.end(), it->first);
	}


	/* dirty, not fresh, spent. */
        if (it->second.coin.IsSpent()) {
	    if (isCoinInLastSnapshot) {
		/* move it from the unspent vector to the spent map. Note that
		 * we must copy the original coin from pcoinsTip because pcoinsTip
		 * is updated after snapshot and has the original coin.
		 * Note that a spent coin will not be moved to the spent set more
		 * than once because we erase it in the unspent set once it is 
		 * spent the first time.
		 */
		Coin coin;
		pcoinsTip->GetCoin(*itUs, coin);
		spent.insert(std::make_pair(std::move(*itUs), coin));
		unspent.erase(itUs);
	    } else {
		added.erase(itUs);
	    }
	    continue;
	}

	/* dirty, not fresh, not spent */
	/* when will this happen? */
//	std::cout << "dirty, not fresh, not spent coin = " << it->first.ToString()
//		<< std::endl;
        if (isCoinInLastSnapshot) {
		/* move it from the unspent vector to the spent map. Note that
		 * we must copy the original coin from pcoinsTip because pcoinsTip
		 * is updated after snapshot and has the original coin. */
		Coin coin;
		pcoinsTip->GetCoin(*itUs, coin);
		spent.insert(std::make_pair(std::move(*itUs), coin));
		unspent.erase(itUs);
	}
	/* record the change in the added set (the current code will allow one 
	 * coin to be insert more than once if it is changed more than once.)*/
	added.emplace_back(it->first.hash, it->first.n);
    }
}

void Snapshot::acceptChunk(std::vector<OutpointCoinPair>& chunk) const {
    /* add all UTXOs in the chunk to the chainstate database. */
    nReceivedUTXOCnt += chunk.size();
    for(OutpointCoinPair& pair: chunk) {
	pcoinsTip->AddCoin(std::move(pair.outpoint), std::move(pair.coin), false);
    }
}

bool Snapshot::IsNewPeerWithValidSnapshot() const {
    uint256 snapshotBlockHash = snpMetadata.blockHeader.GetHash();
    return mapBlockIndex.find(snapshotBlockHash) != mapBlockIndex.end() &&
		mapBlockIndex[snapshotBlockHash]->pprev == nullptr;
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
    for(uint i = 0; i < unspent.size(); i++) {
	ret.append(unspent[i].ToString());
	ret.append(", ");
    }

    ret.append("\nadded.size() = ");
    ret.append(std::to_string(added.size()));
    ret.append("\nadded = ");
    for(uint i = 0; i < added.size(); i++) {
	ret.append(added[i].ToString());
	ret.append(", ");
    }

    ret.append("\nspent.size() = ");
    ret.append(std::to_string(spent.size()));
    ret.append("\nspent = ");
    auto it = spent.begin();
    while(it != spent.end()) {
	ret.append(it->first.ToString());
	it++;
    }
    return ret;
}

void Snapshot::Write2File() const
{
    std::ofstream file;
    file.open("snapshot.out");
    snpMetadata.Serialize(file);
    std::cout << __func__ << ": spent map size = " <<  spent.size() << std::endl;
    for (const auto& pair: spent) {
        pair.first.Serialize(file);
        pair.second.Serialize(file);
    }
    std::cout << __func__ << ": unspent vector size = " <<  unspent.size() << std::endl;
    for (const auto& outpoint: unspent) {
        outpoint.Serialize(file);
    }
}

bool headerEqual(const CBlockHeader& lhs, const CBlockHeader& rhs) {
    return lhs.nVersion == rhs.nVersion && lhs.hashPrevBlock == rhs.hashPrevBlock 
	    && lhs.hashMerkleRoot == rhs.hashMerkleRoot && lhs.nTime == rhs.nTime
	    && lhs.nBits == rhs.nBits && lhs.nNonce == rhs.nNonce;
}

std::unique_ptr<Snapshot> psnapshot;
