#! /bin/bash
../src/bitcoind -datadir=node0 -daemon
../src/bitcoind -datadir=node1 -daemon
sleep 1s
../src/bitcoin-cli -datadir=node0 generatetoaddress 1 "2MxekwcDAfqnyKquvKCRNvSBC8Q5XeAUeWG"

