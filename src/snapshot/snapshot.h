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
#include "coins.h"

class Snapshot{
private:
    std::vector<COutPoint> unspent;
    std::vector<COutPoint> added;
    // TODO: add a hasher in the template list.
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> spent;
public:
    /* load all OutPoints into unspent set. But how do I get all OutPoints? Some 
     * of them are on disk and not in CoinsViewCache. Is there a way to read all
     * of them from disk files?
     */
    Snapshot();
    void initialLoad();
    std::vector<COutPoint> getSnapshot() const;
    uint256 takeSnapshot();
    void addOutPoint(COutPoint op);
    void spendOutPoint(const COutPoint op);
    
    std::string ToString() const;

    // serialization 
};

extern std::unique_ptr<Snapshot> psnapshot;


#endif /* SNAPSHOT_H */

