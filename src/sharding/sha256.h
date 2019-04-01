/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   sha256.h
 * Author: liuyangren
 *
 * Created on April 1, 2019, 4:27 PM
 */

#ifndef SHA256_H
#define SHA256_H

#include <crypto/sha256.h>

/** Compute the 256-bit hash of an object (single hash). This is different than 
 * the hash function Hash() in hash.h because that one computes double hash.
 */
template<typename T1>
inline uint256 singleHash(const T1 pbegin, const T1 pend)
{
    static const unsigned char pblank[1] = {};
    uint256 result;
    CSHA256().Write(pbegin == pend ? pblank : (const unsigned char*)&pbegin[0], (pend - pbegin) * sizeof(pbegin[0]))
              .Finalize((unsigned char*)&result);
    return result;
}

/** Compute the 256-bit hash of the concatenation of one object and a uint32_t number. */
// TODO: test-two nodes should have the same hash output.
template<typename T1>
inline uint256 singleHash(const T1 p1begin, const T1 p1end,
                    const uint32_t num) {
    static const unsigned char pblank[1] = {};
    uint256 result;
    CSHA256().Write(p1begin == p1end ? pblank : (const unsigned char*)&p1begin[0], (p1end - p1begin) * sizeof(p1begin[0]))
		      .Write((const unsigned char*)&num, sizeof(uint32_t))
              .Finalize((unsigned char*)&result);
    return result;
}

#endif /* SHA256_H */

