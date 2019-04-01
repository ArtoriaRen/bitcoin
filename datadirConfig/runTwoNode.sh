#! /bin/bash
../src/bitcoind -datadir=node0 -daemon
../src/bitcoind -datadir=node1 -daemon
sleep 2s
../src/bitcoin-cli -datadir=node0 generatetoaddress 1 " 2MygtEbTBpkagqXkQZYuhD2Are1Ra8nnshg"

