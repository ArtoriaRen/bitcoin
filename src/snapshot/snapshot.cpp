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

static void hashCoin(uint256& result, COutPoint key, Coin& coin){
    std::stringstream ss;
    key.Serialize(ss);
    std::string key_str(ss.str());
    std::cout << __func__ << "key str = " << key_str << std::endl;
    coin.Serialize(ss);
    std::string coin_str(ss.str());
    std::cout << __func__ << "coin str = " << coin_str << std::endl;
    CHash256().Write((const unsigned char*)&key_str, sizeof(key_str.size()))
	    .Write((const unsigned char*)&coin_str, sizeof(coin_str.size()))
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
    merkleRoot = ComputeMerkleRoot(leaves, NULL); 
    return merkleRoot;
}

void Snapshot::addOutPoint(COutPoint op){
    added.push_back(op);
}

void Snapshot::spendOutPoint(const COutPoint op){
    auto it = std::find(unspent.begin(), unspent.end(), op);
    assert(it != unspent.end());
    unspent.erase(it);
    Coin coin;
    pcoinsTip->GetCoin(op, coin);
    spent.insert(std::make_pair(op, coin));
}

std::string Snapshot::ToString() const
{
    std::string ret("merkleroot = ");
    ret.append(merkleRoot.GetHex());
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
