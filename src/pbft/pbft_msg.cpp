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

uint256 TxReq::GetDigest() const {
    return pTx->GetHash();
    //const uint256& tx_hash = pTx->GetHash();
    //uint256 result;
    //CHash256().Write((const unsigned char*)tx_hash.begin(), tx_hash.size())
    //        .Write((const unsigned char*)&type, sizeof(type))
    //        .Finalize((unsigned char*)&result);
    //return result;
}

uint256 LockReq::GetDigest() const {
    const uint256& tx_hash = pTx->GetHash();
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size())
            .Write((const unsigned char*)&type, sizeof(type));
    for (uint i = 0; i < vInputUtxoIdxToLock.size(); i++) {
        hasher.Write((const unsigned char*)&vInputUtxoIdxToLock[i], sizeof(vInputUtxoIdxToLock[i]));
    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
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

UnlockToCommitReq::UnlockToCommitReq(): CClientReq(ClientReqType::UNLOCK_TO_COMMIT) { }

UnlockToCommitReq::UnlockToCommitReq(const CTransactionRef pTxIn, const uint sigCountIn, std::vector<CInputShardReply>&& vReply): CClientReq(ClientReqType::UNLOCK_TO_COMMIT, pTxIn), nInputShardReplies(sigCountIn), vInputShardReply(vReply){}

uint256 UnlockToCommitReq::GetDigest() const {
    const uint256& tx_hash = pTx->GetHash();
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size())
            .Write((const unsigned char*)&type, sizeof(type));
    for (uint i = 0; i < nInputShardReplies; i++) {
        uint256 tmp;
        vInputShardReply[i].getHash(tmp);
        hasher.Write((const unsigned char*)tmp.begin(), tmp.size());
    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

UnlockToAbortReq::UnlockToAbortReq(): CClientReq(ClientReqType::UNLOCK_TO_ABORT) {
    vNegativeReply.resize(CPbft::nFaulty + 1);
}

UnlockToAbortReq::UnlockToAbortReq(const CTransactionRef pTxIn, const std::vector<CInputShardReply>& lockFailReplies): CClientReq(ClientReqType::UNLOCK_TO_ABORT, pTxIn), vNegativeReply(lockFailReplies){
    assert(vNegativeReply.size() == CPbft::nFaulty + 1);
}

uint256 UnlockToAbortReq::GetDigest() const {
    const uint256& tx_hash = pTx->GetHash();
    CHash256 hasher;
    hasher.Write((const unsigned char*)tx_hash.begin(), tx_hash.size())
            .Write((const unsigned char*)&type, sizeof(type));
    for (uint i = 0; i < vNegativeReply.size(); i++) {
        uint256 tmp;
        vNegativeReply[i].getHash(tmp);
        hasher.Write((const unsigned char*)tmp.begin(), tmp.size());
    }
    uint256 result;
    hasher.Finalize((unsigned char*)&result);
    return result;
}

CReqBatch::CReqBatch() { }

CClientReq::CClientReq(const ClientReqType typeIn): type(typeIn) { }
CClientReq::CClientReq(const ClientReqType typeIn, const CTransactionRef pTxIn): type(typeIn), pTx(pTxIn) {}

TxReq::TxReq(): CClientReq(ClientReqType::TX) { }
TxReq::TxReq(const CTransactionRef pTxIn): CClientReq(ClientReqType::TX, pTxIn) { }

LockReq::LockReq(): CClientReq(ClientReqType::LOCK) { }
LockReq::LockReq(const CTransactionRef pTxIn, const std::vector<uint32_t>& vInputUTXOInShard): CClientReq(ClientReqType::LOCK, pTxIn), vInputUtxoIdxToLock(vInputUTXOInShard) { }
