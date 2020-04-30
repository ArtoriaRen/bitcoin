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

Snapshot::Snapshot() {
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
	leaves.push_back(SerializeHash(unspent[i]));
    }
    return  ComputeMerkleRoot(leaves, NULL);
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
    std::string ret("unspent = \n");
    for(uint i = 0; i < unspent.size(); i++) {
	ret.append(unspent[i].ToString());
	ret.append(", ");
    }
    ret.append("\nadded = \n");
    for(uint i = 0; i < added.size(); i++) {
	ret.append(added[i].ToString());
	ret.append(", ");
    }
    ret.append("\nspent = \n");
    auto it = spent.begin();
    while(it != spent.end()) {
	ret.append(it->first.ToString());
	it++;
    }
    return ret;
}

std::unique_ptr<Snapshot> psnapshot;
