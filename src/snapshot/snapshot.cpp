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
std::vector<COutPoint> Snapshot::getSnapshot() const {
    std::vector<COutPoint> snapshot(unspent);
    auto iter = spent.begin();
    while (iter != spent.end()) {
	snapshot.push_back(iter->first);
	iter++;
    }
    return snapshot;
}

uint256 Snapshot::takeSnapshot() {
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
    return lastSnapshotMerkleRoot;
}

void Snapshot::addCoins(const CCoinsMap& mapCoins){
    for (CCoinsMap::const_iterator it = mapCoins.begin(); it != mapCoins.end(); it++) {
        // Ignore non-dirty entries.
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            continue;
        }
        std::vector<COutPoint>::iterator itUs = std::find(unspent.begin(), unspent.end(), it->first);
        if (itUs == unspent.end()) {
            // The unspent vector does not have an entry, while the mapCoins does
            // We can ignore it if it's both FRESH and pruned (spent) in the mapCoins
            if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
                // Otherwise we will need to create it in the added vector
		added.emplace_back(it->first.hash, it->first.n);
            }
        } else {
            // Found the entry in the parent cache
            if (it->second.coin.IsSpent()) {
		// move it from the unspent vector to the spent map
		spent.insert(std::make_pair(std::move(*itUs), it->second.coin));
		unspent.erase(itUs);
            } else {
                // A normal modification.
		/* when will this happen? Why a coin is modified? */
            }
        }
    }
}

void Snapshot::spendCoin(const COutPoint& op){
    auto it = std::find(unspent.begin(), unspent.end(), op);
    assert(it != unspent.end());
    Coin coin;
    pcoinsTip->GetCoin(*it, coin);
    spent.insert(std::make_pair(std::move(*it), coin));
    unspent.erase(it);
}

void Snapshot::receiveSnapshot(CDataStream& vRecv) {
    if(unspent.empty())
	vRecv >> unspent;
}

std::string Snapshot::ToString() const
{
    std::string ret("lastSnapshotMerkleRoot = ");
    ret.append(lastSnapshotMerkleRoot.GetHex());
    ret.append("\nunspent.size() = ");
    ret.append(std::to_string(unspent.size()));
    ret.append("\nunspent = ");
    for(uint i = 0; i < unspent.size(); i++) {
	ret.append(unspent[i].ToString());
	ret.append(", ");
    }
    ret.append("\nadded = ");
    for(uint i = 0; i < added.size(); i++) {
	ret.append(added[i].ToString());
	ret.append(", ");
    }
    ret.append("\nspent = ");
    auto it = spent.begin();
    while(it != spent.end()) {
	ret.append(it->first.ToString());
	it++;
    }
    return ret;
}

std::unique_ptr<Snapshot> psnapshot;
