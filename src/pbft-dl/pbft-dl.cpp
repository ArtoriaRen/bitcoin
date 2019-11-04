#include <sstream> 
#include <stdint.h>
#include "pbft-dl/debug_flags.h"
#include "pbft-dl/pbft-dl.h"
#include "cert.h"
#include "pbft2_5.h"

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
    //TODO: check if all commits are of the same seq (this step can be saved if we compress the GPP by using shared seq instead of a seq in every commit msg.)
    // check all sig in the leader group local-CC 
    uint32_t localViewStd = INT_MAX;
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
	if(localViewStd == INT_MAX){
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

bool DL_pbft::checkGP(CCrossGroupMsg& gpMsg, uint32_t currentGV, const std::vector<DL_LogEntry>& log){
    // check all sig in the leader group local-CC 
    uint32_t localViewStd = INT_MAX;
    for(auto commit : gpMsg.localCC){
	if(pkMap.find(commit.senderId) == pkMap.end()){
	    std::cerr<< "checking a commit msg in the GP message: no pub key for the sender" << std::endl;
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
	if(localViewStd == INT_MAX){
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
	 * The corresponding log entry should have a pre_prepare because GPP should arrive first and trigger localPP msg. 
	 * placeholder: faulty followers may accept.
	 */
	// TODO: how to tolerate GP arrives before GPP arrives? delay the verification of matching digest?
	if(!log[commit.seq].pre_prepare.digest.IsNull() && log[commit.seq].pre_prepare.digest != commit.digest){
	    std::cerr<< "digest error. digest in log = " << log[commit.seq].pre_prepare.digest.GetHex() << ", but commit digest = " << commit.digest.GetHex() << std::endl;
	    return false;
	}
	
#ifdef CROSS_GROUP_DEBUG
    	std::cout << "dl sanity check of sender" << commit.senderId << "'s commit succeed" << std::endl;
#endif
    }
    return true;
    
}

bool DL_pbft::checkGC(CCrossGroupMsg& gcMsg, uint32_t currentGV, const std::vector<DL_LogEntry>& log){
    // check all sig of nodes in all groups whose gplc is in the GC message
    uint32_t localViewStd = INT_MAX;
    for(auto commit : gcMsg.localCC){
	if(pkMap.find(commit.senderId) == pkMap.end()){
	    std::cerr<< "checking a commit msg in the GC message: no pub key for the sender" << std::endl;
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
	if(localViewStd == INT_MAX){
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
	 * The corresponding log entry should have a pre_prepare because GPP should arrive first and trigger localPP msg. 
	 * placeholder: faulty followers may accept.
	 */
	// TODO: how to tolerate GP arrives before GPP arrives? delay the verification of matching digest?
	if(!log[commit.seq].pre_prepare.digest.IsNull() && log[commit.seq].pre_prepare.digest != commit.digest){
	    std::cerr<< "digest error. digest in log = " << log[commit.seq].pre_prepare.digest.GetHex() << ", but commit digest = " << commit.digest.GetHex() << std::endl;
	    return false;
	}
	
#ifdef CROSS_GROUP_DEBUG
    	std::cout << "dl sanity check of sender" << commit.senderId << "'s commit succeed" << std::endl;
#endif
    }
    return true;
    
}

void DL_pbft::sendGlobalMsg2Leaders(const CCrossGroupMsg& msg, UdpClient& udpClient, CPbft2_5* pbftObj){
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

    // virtually send the message to this node itself if the msg is GP or GC.
    switch(msg.phase){
	case DL_GP:
	    pbftObj->onReceiveGP(const_cast<CCrossGroupMsg&>(msg), false);
	    break;
	case DL_GC:
	    pbftObj->onReceiveGC(const_cast<CCrossGroupMsg&>(msg), false);
	    break;
	default:
	    break;
    }
}

void DL_pbft::multicastCert(const CCertMsg& cert, UdpClient& udpClient, const std::unordered_map<uint32_t, CPbftPeer>& peers){
    std::ostringstream oss;
    cert.serialize(oss);
#ifdef SERIAL 
    std::cout << "oss size() = " << oss.str().size() << std::endl; 
#endif
    for(auto groupMate : peers){
#ifdef CROSS_GROUP_DEBUG
	std::cout << "send Cert to peer " << groupMate.first << std::endl;
#endif
	
	udpClient.sendto(oss, groupMate.second.ip, groupMate.second.port);
    }
}

bool DL_pbft::checkGPCD(const CCertMsg& cert, uint32_t currentGV, const std::vector<DL_LogEntry>& log){
    unsigned int gppCnt = 0, gpCnt = 0;
    bool valid = false;
    for(auto lc: cert.globalCert){ // each element in globalCert is a local commit msg.
	if(lc.phase == DL_GPP){
	    gppCnt++;
	    valid = checkGPP(lc, currentGV, log);
	} else {
	    gpCnt++;
	    valid = checkGP(lc, currentGV, log);
	}
	if(!valid)
	    return false;
    }
    if(gppCnt == 1 && gpCnt == cert.certSize - 1)
	return true;
    return false;
}

bool DL_pbft::checkGCCD(const CCertMsg& cert, uint32_t currentGV, const std::vector<DL_LogEntry>& log){
    bool valid = false;
    for(auto lc: cert.globalCert){ // each element in globalCert is a local commit msg.
        valid = checkGC(lc, currentGV, log);
	if(!valid)
	    return false;
    }
    return true;
}