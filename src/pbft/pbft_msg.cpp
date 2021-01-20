#include "pbft/pbft_msg.h"
#include "hash.h"
#include "pbft.h"

uint32_t MAX_BATCH_SIZE = 100; 

CReply::CReply(): reply(), digest(), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

CReply::CReply(char replyIn, const uint256& digestIn): reply(replyIn), digest(digestIn), sigSize(0), vchSig(){ 
    vchSig.reserve(72); // the expected sig size is 72 bytes.
}

void CReply::getHash(uint256& result) const {
    CHash256().Write((const unsigned char*)&reply, sizeof(reply))
	    .Write(digest.begin(), sizeof(digest))
	    .Finalize((unsigned char*)&result);
}

CInputShardReply::CInputShardReply(): CReply() {};

CInputShardReply::CInputShardReply(char replyIn, const uint256& digestIn, CAmount valueIn):
    CReply(replyIn, digestIn), totalValueInOfShard(valueIn) {};

void CInputShardReply::getHash(uint256& result) const {
    uint256 tmp;
    CReply::getHash(tmp);
    CHash256().Write((const unsigned char*)tmp.begin(), tmp.size())
	    .Write((const unsigned char*)&totalValueInOfShard, sizeof(totalValueInOfShard))
	    .Finalize((unsigned char*)&result);
}

UnlockToCommitReq::UnlockToCommitReq(): CClientReq(CMutableTransaction()) {}

UnlockToCommitReq::UnlockToCommitReq(const CTransaction& txIn, const uint sigCountIn, std::vector<CInputShardReply>&& vReply) : CClientReq(txIn), nInputShardReplies(sigCountIn), vInputShardReply(vReply){}

uint256 TxReq::GetDigest() const {
    return tx_mutable.GetHash();
}

uint256 LockReq::GetDigest() const {
    uint256 tx_hash(tx_mutable.GetHash());
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size());
    for (uint i = 0; i < nOutpointToLock; i++) {
	hasher .Write((const unsigned char*)&vInputUtxoIdxToLock[i], sizeof(vInputUtxoIdxToLock[i]));
    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

uint256 UnlockToCommitReq::GetDigest() const {
    uint256 tx_hash(tx_mutable.GetHash());
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size());
    for (uint i = 0; i < nInputShardReplies; i++) {
	uint256 tmp;
	vInputShardReply[i].getHash(tmp);
	hasher.Write((const unsigned char*)tmp.begin(), tmp.size());

    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

UnlockToAbortReq::UnlockToAbortReq(): CClientReq(CMutableTransaction()) {
    vNegativeReply.resize(CPbft::nFaulty + 1);
}

UnlockToAbortReq::UnlockToAbortReq(const CTransaction& txIn, const std::vector<CInputShardReply>& lockFailReplies) : CClientReq(txIn), vNegativeReply(lockFailReplies){
    assert(vNegativeReply.size() == CPbft::nFaulty + 1);
}

uint256 UnlockToAbortReq::GetDigest() const {
    uint256 tx_hash(tx_mutable.GetHash());
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size());
    for (uint i = 0; i < vNegativeReply.size(); i++) {
	uint256 tmp;
	vNegativeReply[i].getHash(tmp);
	hasher.Write((const unsigned char*)tmp.begin(), tmp.size());

    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

CClientReq::CClientReq(const CTransaction& tx) : tx_mutable(tx), hash(tx.GetHash()) {
}

void CClientReq::UpdateHash() {
    hash = tx_mutable.GetHash();
}

const uint256& CClientReq::GetHash() const {
    return hash;
}

bool CClientReq::IsCoinBase() const {
    return (tx_mutable.vin.size() == 1 && tx_mutable.vin[0].prevout.IsNull());
}

TypedReq::TypedReq(ClientReqType typeIn, std::shared_ptr<CClientReq> pReqIn): type(typeIn), pReq(pReqIn) { }

uint256 TypedReq::GetHash() const {
    uint256 req_hash(pReq->GetDigest());
    uint256 result;
    CHash256().Write((const unsigned char*)req_hash.begin(), req_hash.size()).Write((const unsigned char*)type, sizeof(type)).Finalize((unsigned char*)&result);
    return result;
}

CReqBatch::CReqBatch(){ }

void CReqBatch::emplace_back(ClientReqType typeIn, std::shared_ptr<CClientReq> pReqIn) {
    vReq.emplace_back(typeIn, pReqIn);
}
