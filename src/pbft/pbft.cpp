/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */


#include "pbft.h"


/**
 * Constructor. Go through the last nBlocks block, calculate membership of nGroups groups.
 * @param random is the random number used to group nodes.
 * @param nBlocks is number of blocks whose miner participate in the PBFT.
 * @return 
 */
void CPbft::group(uint32_t randomNumber, uint32_t nBlocks, const CBlockIndex* pindexNew) {
    const CBlockIndex* pindex = pindexNew; // make a copy so that we do not change the original argument passed in
    LogPrintf("group number %d nBlock = %d, pindex->nHeight = %d \n", nGroups, nBlocks, pindex->nHeight);
    for (uint i = 0; i < nBlocks && pindex != nullptr; i++) {
        //TODO: get block miner IP addr and port, add them to the members
        LogPrintf("pbft: block height = %d, ip&port = %s \n ", pindex->nHeight, pindex->netAddrPort.ToString());
        pindex = pindex->pprev;
    }

    nFaulty = (members.size() - 1)/3;
}


CPre_prepare CPbft::assemblePre_prepare(const CBlock& block){
    return CPre_prepare();
}

CPrepare CPbft::assemblePrepare(const uint256& digest){
    return CPrepare();
}

CCommit CPbft::assembleCommit(const uint256& digest){
    return CCommit();
}


bool CPbft::onReceivePrePrepare(const CPre_prepare& pre_prepare){
    std::cout<< "received pre-prepare" << std::endl;
    // verify signature and return wrong if big is wrong

    // add to log
    log[pre_prepare.digest.ToString()] = CPbftLog(pre_prepare);
    sendPrepare();
    return true;
}

bool CPbft::onReceivePrepare(const CPrepare& prepare){
    std::cout << "received prepare. Phase  = " << log[prepare.digest.ToString()].phase << std::endl;
    //verify sig. if wrong, return false.


    //add to log
    log[prepare.digest.ToString()].prepareArray.push_back(prepare);
    // count the number of prepare msg. enter commit if greater than 2f
    if(log[prepare.digest.ToString()].phase == PbftPhase::prepare && log[prepare.digest.ToString()].prepareArray.size() >= (nFaulty << 1) ){

	// enter commit phase
	std::cout << "enter commit phase" << std::endl;
	log[prepare.digest.ToString()].phase = PbftPhase::commit;
	    sendCommit();
	    return true;
    }
    return true;
}

bool CPbft::onReceiveCommit(const CCommit& commit){
    std::cout << "received commit" << std::endl;
    //verify sig. if wrong, return false.

    //add to log
    log[commit.digest.ToString()].commitArray.push_back(commit);
    // count the number of prepare msg. enter reply if greater than 2f+1
    if(log[commit.digest.ToString()].phase == PbftPhase::commit && log[commit.digest.ToString()].commitArray.size() >= (nFaulty << 1 ) + 1 ){

	// enter commit phase
	std::cout << "enter reply phase" << std::endl;
	log[commit.digest.ToString()].phase = PbftPhase::reply;
	    excuteTransactions(commit.digest);
	    return true;
    }
    return true;
}

void CPbft::sendPrepare(){
   // send prepare to all nodes in the members array.
   std::cout << "sending Prepare..." << std::endl;
}

void CPbft::sendCommit(){
   std::cout << "sending Commit..." << std::endl;
}

void CPbft::excuteTransactions(const uint256& digest){
   std::cout << "executing tx..." << std::endl;
}