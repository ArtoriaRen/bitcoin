/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <tx_placement/tx_placer.h>
#include <queue>
#include <fstream>
#include <string> 

#include "hash.h"
#include "arith_uint256.h"
#include "chain.h"
#include "validation.h"
#include "chainparams.h"
#include "txdb.h"
#include "rpc/server.h"
#include "core_io.h" // HexStr


/* global variable configurable from conf file. */
uint32_t randomPlaceBlock;
uint32_t blockEnd;
uint32_t num_committees;
int lastAssignedAffinity = -1;

