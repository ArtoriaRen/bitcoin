#include <sstream> 
#include <stdint.h>
#include "pbft-dl/debug_flags.h"
#include "pbft-dl/pbft-dl.h"

DL_pbft::DL_pbft():globalLeader(0){
}

/**
 * 
 * @param gppMsg
 * @param currentGV current global view
 * @param log
 * @return 
 */
bool DL_pbft::checkGPP(CCrossGroupMsg& gppMsg, uint32_t currentGV, const std::vector<DL_LogEntry>& log){
    uint256 reqHash = Hash(gppMsg.clientReq.begin(), gppMsg.clientReq.end());
    // check all sig in the leader group local-CC 
    // assemble local pre-prepare.
    int localViewStd = -1;
    for(auto commit : gppMsg.localCC){
	if(pkMap.find(commit.senderId) == pkMap.end()){
	    std::cerr<< "cross group: no pub key for the sender" << std::endl;
	    return false;
	}
	// verify signature and return wrong if sig is wrong
	uint256 msgHash;
	commit.getHash(msgHash);
	if(!pkMap[commit.senderId].Verify(msgHash, commit.vchSig)){
	    std::cerr<< "verification sender" << commit.senderId <<"'s sig fail" << std::endl;
	    return false;
	} 
	// all commits should be in the same local view. 
	if(localViewStd == -1){
	    // this is the first commit msg in the localCC, we should set localViewStd
	    localViewStd = commit.localView;
	} else if(commit.localView != localViewStd){
	    std::cerr<< "commit local view = " << commit.localView << ", but localViewStd = " << localViewStd << std::endl;
	    return false;
	}
	
	// this server should be in the global view of all commits in the localCC.
	if(commit.globalView != currentGV){
	    std::cerr<< "commit global view = " << commit.globalView << ", but current global view = " << currentGV  << std::endl;
	    return false;
	}
	
	/* check if the seq is alreadly attached to another digest.
	 * The corresponding log entry should be empty when a GPP arrives. 
	 * placeholder: faulty followers may accept.
	 */
	if(!log[commit.seq].pre_prepare.digest.IsNull() && log[commit.seq].pre_prepare.digest != commit.digest){
	    std::cerr<< "digest error. digest in log = " << log[commit.seq].pre_prepare.digest.GetHex() << ", but commit digest = " << commit.digest.GetHex() << std::endl;
	    return false;
	}
	
	//  check if the digest matches client req
	if(commit.digest != reqHash){
	    std::cerr<< "digest does not match client request. client req = " << gppMsg.clientReq << ", but digest = " << commit.digest.GetHex() << std::endl;
	    return false;
	    
	}
#ifdef CROSS_GROUP_DEBUG
    	std::cout << "dl sanity check of sender" << commit.senderId << "'s commit succeed" << std::endl;
#endif
    }
    return true;
    
}

bool DL_pbft::checkGP(CCrossGroupMsg& msg){
    return true;
    
}

bool DL_pbft::checkGC(CCrossGroupMsg& msg){
    return true;
    
}

void DL_pbft::sendGPP2Leaders(const CCrossGroupMsg& msg, UdpClient& udpClient){
    std::ostringstream oss;
    msg.serialize(oss);
#ifdef SERIAL 
    std::cout << "oss size() = " << oss.str().size() << std::endl; 
#endif
    for(auto p : peerGroupLeaders){
#ifdef CROSS_GROUP_DEBUG
	std::cout << "send GPP to peer " << p.first << std::endl;
#endif
	
	udpClient.sendto(oss, p.second.ip, p.second.port);
    }
}


void DL_pbft::sendMsg2Leaders(const CCrossGroupMsg& msg){
}
