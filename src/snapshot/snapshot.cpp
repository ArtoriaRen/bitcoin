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

static const uint32_t MB = 1000000; // limit the chunks msg size below 4M msg limit.


OutpointCoinPair::OutpointCoinPair(){ }

OutpointCoinPair::OutpointCoinPair(COutPoint opIn, Coin coinIn): outpoint(opIn), coin(coinIn){ }

uint256 SnapshotMetadata::getHeaderHash() const {
    CHash256 hasher; 	
    uint256 hash;
    hasher.Write((const unsigned char*)&height, sizeof(height))
	    .Write(snapshotBlockHash.begin(), snapshotBlockHash.size())
	    .Write((const unsigned char*)&nChunks, sizeof(nChunks))
	    .Write((const unsigned char*)&chainTx, sizeof(chainTx));
    hasher.Finalize((unsigned char*)&hash);
    return hash;
}

Snapshot::Snapshot(): receivedChunkCnt(0), nReceivedUTXOCnt(0), notYetDownloadSnapshot(true) { }

void Snapshot::sendChunk(CNode* pfrom, const CNetMsgMaker& msgMaker, CConnman* connman, uint32_t chunkId) const {
    connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::CHUNK, chunkId, vSerializedChunks[chunkId]));
    LogPrint(BCLog::NET, "send chunk id = %d, size = %u\n", chunkId, vSerializedChunks[chunkId].size());
}

//void Snapshot::appendToHeaderChain(const std::vector<CBlockHeader>& headers) {
//    headerChain.insert(headerChain.end(), headers.begin(), headers.end());
//}

bool Snapshot::verifyMetadata(const SnapshotMetadata& metadata) const {
    uint256 headerHash = metadata.getHeaderHash();
    /* calculate snapshot hash */
    CHash256 hasher; 	
    uint256 hash;
    hasher.Write(headerHash.begin(), headerHash.size());
    for (const auto& h: metadata.vChunkHash){
	hasher.Write(h.begin(), h.size());
    }
    hasher.Finalize((unsigned char*)&hash);
    return hash == metadata.snapshotHash; 
}

bool Snapshot::verifyChunk(const uint32_t chunkId, const std::string& chunk) const {
    CHash256 hasher; 	
    uint256 hash;
    hasher.Write((const unsigned char*)chunk.data(), chunk.size());
    hasher.Finalize((unsigned char*)&hash);
    return hash == snpMetadata.vChunkHash[chunkId]; 
}

uint256 Snapshot::takeSnapshot() {
    /* Store the OutPoint part of all coins into a vector for sorting. */
    std::vector<COutPoint> vAllOutPoints;
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
    assert(pcursor);
    
    // iterate chain state database
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        if (pcursor->GetKey(key)) {
            vAllOutPoints.push_back(std::move(key));
        } else {
            std::cout << __func__ << ": unable to read key" << std::endl;
        }
        pcursor->Next();
    }

    std::sort(vAllOutPoints.begin(), vAllOutPoints.end());

    /* create 1MB chunks of serialized UTXOs. */
    std::stringstream ss;
    for (uint i = 0; i < vAllOutPoints.size(); i++) {
	COutPoint key = vAllOutPoints[i];
	Coin coin;
	bool found = pcoinsTip->GetCoin(key, coin);
	/* unspent coins must exist */
	assert(found);
	key.Serialize(ss);
	coin.Serialize(ss);
    }
    char chunk_charArr[MB];
    while (!ss.eof()) {
	ss.read(chunk_charArr, MB);
	vSerializedChunks.push_back(std::string(chunk_charArr, MB));
    }
    if (ss.gcount() > 0) {
	vSerializedChunks.push_back(std::string(chunk_charArr, ss.gcount()));
    }
    snpMetadata.vChunkHash.resize(vSerializedChunks.size());

    /* fill snapshot header */
    if (chainActive.Tip()) {
	snpMetadata.height = chainActive.Tip()->nHeight;
	snpMetadata.snapshotBlockHash = chainActive.Tip()->GetBlockHash();
	snpMetadata.nChunks = vSerializedChunks.size();
	snpMetadata.chainTx = chainActive.Tip()->nChainTx;
    }

    /* calculate header and chunk hashes. */
    uint256 headerHash = snpMetadata.getHeaderHash();
    for (uint i = 0; i <vSerializedChunks.size(); i++) {
	CHash256 hasher; 	
	hasher.Write((const unsigned char*)vSerializedChunks[i].data(), vSerializedChunks[i].size());
	hasher.Finalize((unsigned char*)&snpMetadata.vChunkHash[i]);
    }

    /* calculate snapshot hash */
    CHash256 hasher; 	
    hasher.Write(headerHash.begin(), headerHash.size());
    for (const auto& h: snpMetadata.vChunkHash){
	hasher.Write(h.begin(), h.size());
    }
    hasher.Finalize((unsigned char*)&snpMetadata.snapshotHash);
    return snpMetadata.snapshotHash;
}


void Snapshot::applySnapshot() const {
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    for (auto& chunk : vSerializedChunks) {
	ds.write(chunk.data(), chunk.size());
    }
    /* Unserialize UTXOs and add them to the chainstate database. */
    while (!ds.eof()) {
	COutPoint key;
	Coin coin;
	ds >> key;
	ds >> coin;
	pcoinsTip->AddCoin(std::move(key), std::move(coin), false);
	nReceivedUTXOCnt++;
    }
}

std::string Snapshot::ToString() const
{
    std::string ret;
    uint256 snapshotBlockHash = snpMetadata.snapshotBlockHash;
    if (!snapshotBlockHash.IsNull()) {
		ret.append("snapshot block = ");
		ret.append(snapshotBlockHash.GetHex());
		ret.append("\nheight = ");
		ret.append(std::to_string(snpMetadata.height));
		ret.append("\n");
    } 

    ret.append("snapshot hash = ");
    ret.append(snpMetadata.snapshotHash.GetHex());


    ret.append("\n chunk sizes = ");
    for (uint i = 0; i < vSerializedChunks.size(); i++) {
	ret.append("id = " + std::to_string(i));
	ret.append(", size = " + std::to_string(vSerializedChunks.size()) + "\n");
    }
    return ret;
}

void Snapshot::Write2File() const
{
    std::ofstream file;
    file.open("snapshot_coinPrune.out");
    snpMetadata.Serialize(file);
    for (uint i = 0; i < vSerializedChunks.size(); i++) {
	file << vSerializedChunks[i];
    }
}

std::unique_ptr<Snapshot> psnapshot;
