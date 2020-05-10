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

OutpointCoinPair::OutpointCoinPair(){ }

OutpointCoinPair::OutpointCoinPair(COutPoint opIn, Coin coinIn): op(opIn), coin(coinIn){ }

static void hashCoin(uint256& result, COutPoint key, Coin& coin){
    std::stringstream ss;
    key.Serialize(ss);
    std::string key_str(ss.str());
    ss.str("");
    coin.Serialize(ss);
    std::string coin_str(ss.str());
    CHash256().Write((const unsigned char*)key_str.c_str(), sizeof(key_str.size()))
	    .Write((const unsigned char*)coin_str.c_str(), sizeof(coin_str.size()))
	    .Finalize((unsigned char*)&result);
}

Snapshot::Snapshot() {
    unspent.reserve(pcoinsTip->GetCacheSize());
    /*default is take one snapshot per 10 blocks. In reality, this will cause too
     * much overhead. Probably 1000 ~ 10000 is a good value. */
    frequency = 10; 
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

/* TODO: send coins over as well. currently only send outpoints. */
std::vector<OutpointCoinPair> Snapshot::getSnapshot() const {
    std::vector<OutpointCoinPair> snapshot;
    snapshot.reserve(unspent.size() + spent.size());
    for (COutPoint op: unspent){
        Coin coin;
        bool found = pcoinsTip->GetCoin(op, coin);
	/* unspent coins must exist */
	assert(found);
	snapshot.emplace_back(op, coin);
    }
    auto iter = spent.begin();
    while (iter != spent.end()) {
	snapshot.emplace_back(iter->first, iter->second);
	iter++;
    }
    return snapshot;
}

uint256 Snapshot::takeSnapshot(bool updateBlkInfo) {
    /* Should use lock to stop other threads reading snaphshot, but in current test,
     * we only get snapshot after we know one has been generate.*/
    unspent.insert(unspent.end(), added.begin(), added.end());
    added.clear();
    spent.clear();
    std::sort(unspent.begin(), unspent.end());
    std::vector<uint256> leaves;
    leaves.reserve(unspent.size());
    for (uint i = 0; i < unspent.size(); i++) {
	COutPoint key = unspent[i];
        Coin coin;
        bool found = pcoinsTip->GetCoin(key, coin);
	/* unspent coins must exist */
	assert(found);
	uint256 hash;
	hashCoin(hash, key, coin);
	leaves.push_back(hash);
    }
    lastSnapshotMerkleRoot = ComputeMerkleRoot(leaves, NULL); 
    /* note down which block encloses the snapshot. We only do this if we are an
     * old peer and is producing snapshot. For new peer using this function 
     * calculating snapshot merkle root, there is no need to update.*/
    if (updateBlkInfo && chainActive.Tip()){
	headerNheight.header = chainActive.Tip()->GetBlockHeader();
        headerNheight.height = chainActive.Tip()->nHeight;
	headerNheight.snapshotMerkleRoot = lastSnapshotMerkleRoot;
	snapshotBlockHash = headerNheight.header.GetHash();
    } else if (!updateBlkInfo && chainActive.Tip()) {
	assert(headerNheight.height == chainActive.Tip()->nHeight);
    }
    return lastSnapshotMerkleRoot;
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
	std::cout << "dirty, not fresh, not spent coin = " << it->first.ToString()
		<< std::endl;
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

//        std::vector<COutPoint>::iterator itUs = std::find(unspent.begin(), unspent.end(), it->first);
//        if (itUs == unspent.end()) {
//            // The unspent vector does not have an entry, while the mapCoins does
//            // We can ignore it if it's both FRESH and pruned (spent) in the mapCoins
//            if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
//                // Otherwise we will need to create it in the added vector
//		/* The entry may have the same outpoint as some entry in the
//		 * unspent dataset. Should let this entry overwrite the entry 
//		 * in the unspent dataset when merge them when taking snapshot.
//		 * Will entry als overwrite another entry in the added set?
//		 */
//		added.emplace_back(it->first.hash, it->first.n);
//            } 
//        } else {
//            // Found the entry in the parent cache
//            if (it->second.coin.IsSpent()) {
//		// move it from the unspent vector to the spent map
//		spent.insert(std::make_pair(std::move(*itUs), it->second.coin));
//		unspent.erase(itUs);
//            } else {
//                // A normal modification.
//		/* when will this happen? Why a coin is modified? */
//            }
//        }

	
    }
}

//void Snapshot::spendCoin(const COutPoint& op){
//    std::cout << __func__ << ": " << op.ToString() << std::endl;
//    auto it = std::find(unspent.begin(), unspent.end(), op);
//    if(it != unspent.end()){
//	Coin coin;
//	pcoinsTip->GetCoin(*it, coin);
//	spent.insert(std::make_pair(std::move(*it), coin));
//	unspent.erase(it);
//	return;
//    }
//    /* The coin is not in the unspent set, it must be in the added set. 
//     * Then there is not need to move it to the spent set because it is not
//     * in the last snapshot.
//     */
//    auto itAdded = std::find(added.begin(), added.end(), op);
//    assert (itAdded != added.end());
//    added.erase(itAdded);
//}

void Snapshot::receiveSnapshot(CDataStream& vRecv) {
    /* Step 1: update snapshot data and the coins view cache in memory */
    std::vector<OutpointCoinPair> snapshot;
    vRecv >> snapshot;
    /* only a new peer ask for snapshot */
    assert(unspent.empty());
    for(auto p: snapshot) {
	unspent.push_back(p.op);
	pcoinsTip->AddCoin(p.op, std::move(p.coin), false);
    }

    /* Step 2: build the snapshot merkle root and compare it to the one received. 
     * if they are the same, we are good to go to step 3; otherwise, either we
     * unserialize the data wrong or the peer we contact has lied. 
     */
    takeSnapshot(false);
    assert(lastSnapshotMerkleRoot == headerNheight.snapshotMerkleRoot);

    /* Step 3: update the best coin in the coins view cache.
     */
    assert(!snapshotBlockHash.IsNull()); // snapshot block hash should have been updated in the SNAPSHOT_BLK_HEADER message
    pcoinsTip->SetBestBlock(snapshotBlockHash);
}

std::string Snapshot::ToString() const
{
    std::string ret;
    if (!snapshotBlockHash.IsNull()) {
		ret.append("snapshot block = ");
		ret.append(snapshotBlockHash.GetHex());
		ret.append("\nheight = ");
		ret.append(std::to_string(headerNheight.height));
		ret.append("\n");
    } 

    ret.append("lastsnapshotmerkleroot = ");
    ret.append(lastSnapshotMerkleRoot.GetHex());

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

std::unique_ptr<Snapshot> psnapshot;
