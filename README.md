Research on Blockchain
=====================================

# Smart Transaction Placement (STP) Overview
----------------
STP is a transaction placement algorithm for blockchain sharding protocols. It improves the performance by reducing cross-shard transactions. 
The paper compares the performance of STP with that of Hashing Placment(HP).

# Source Code
This branch (`omniledger_smartPlace_client`) is for STP client code.
See branch [tx_place_pbft_omniledger_smartPlace](https://github.com/ArtoriaRen/bitcoin/tree/tx_place_pbft_omniledger_smartPlace) for STP server code.
See branch [tx_place_omniledger_block_allowAbort](https://github.com/ArtoriaRen/bitcoin/tree/tx_place_omniledger_block_allowAbort) for HP client code.
See branch [tx_place_client_correct_throughput_measure](https://github.com/ArtoriaRen/bitcoin/tree/tx_place_client_correct_throughput_measure) for HP server code.


# Reproducing the 2-shard Exprimental Result in the paper
-------
1. Install Dependencies of Bitcoin Core 
See [Rich Apodaca's post](https://bitzuma.com/posts/compile-bitcoin-core-from-source-on-ubuntu/).

2. Download the blockchain of Bitcoin
Experiments in the paper use the system state at block height 600999. [Here](https://github.com/ArtoriaRen/bitcoin/tree/0.16_controlled_sync) is the Bitcoin Core code with minor modification to download blocks to a specific height. 
And [here]() is the configuration file (i.e. `sync_from_public.conf`) where you specify the height using the `synctoheight` field. Follow the steps below to download the Bitcoin blockchain.
- download the above source code, i.e., the `0.16_controlled_sync` branch, compile it.
- Create an empty folder, e.g. named `blocks_600999`, rename the above `sync_from_public.conf` file to `bitcoin.conf`, and put it into the empty folder.
- Start `bitcoind` to download blocks:  `<git_repo_folder>/src/bitcoind --datadir=blocks_600999`. 

Make sure your machine has Internet access. Once started, `bitcoind` will automatically connect to some Bitcoin peers and sync blocks from them. Depending on your CPU and network bandwidth, the downloading process may last several hours to several days.

3. Set Shard Affinities of UTXOs
At the beginning of the experiments, UTXOs are evenly distributed among shards, so we need to assign all UTXOs in the `chainstate` database of height 600999 to 2 shards. Follow the steps below.
- shutdown `bitcoind` after downloading blocks: `<git_repo_folder>/src/bitcoin-cli --datadir=blocks_600999 stop`.
- make a copy of the `blocks_600999/chainstate` folder and store it somewhere because we are going to modify the UTXOs, which are stored in this folder.
- replace the `bitcoin.conf` with [this file](), where you can specify the number of shards by setting the `numcommittees` field. This file also sets `connect=0` so that the peer will not initiate outbound connection to Bitcoin peers. Make sure the new file is renamed as `bitcoin.conf`.
- checkout the `omniledger_smartPlace_client` branch, comment out line 80 of file `src/coins.h` (i.e., `//::Unserialize(s, shardAffinity);`). This change is only for reading in UTXOs without the `shardAffinity` field. This feature is only required for assigning affinities. We will change it back later when running experiments.
- compile the STP client code.
- start the STP-client-version bitcoind: `<git_repo_folder>/src/bitcoind --datadir=blocks_600999`
- assign UTXOs evenly to shard: `<git_repo_folder>/src/bitcoin-cli --datadir=blocks_600999 assignaffinity`.
- once the RPC returns, shutdown the bitcoind instance. Then move the `blocks_600999/chainstate` folder outside the `blocks_600999` folder and rename it as `chainstate_withAffinity_2shard`, and move back the orginal `chainstate` folder.
- uncomment line 80 of file `src/coins.h` (i.e., `::Unserialize(s, shardAffinity);`), and compile the code again.

4. Set Up a 2-shard Bitcoin Network
Servers run the server-version bitcoind, so you need to compile the `tx_place_pbft_omniledger_smartPlace` branch. But before checking out the branch and compile, be sure to make a copy of the STP-client-version `bitcoind` and `bitcoin-cli`, because they will be overwritten when we compile the STP-server-version countparts.  

Every shard has 4 servers, so measuring the performance of two shards requires 9 machines (or VMs) in total: 8 servers and 1 client. Every machine must have its own bitcoin data folder, so copy the `blocks_600999/` folder to all machines. Also, copy the STP-server-version `bitcoind` to all servers, and the the STP-client-version `bitcoind` to the client.

Nodes connect to each other using port 8330, and PBFT leaders also listen on port 28830 for client connection. Every node has a distinct `pbftid` in their `bitcoin.conf` file. Client use `pbftid = 65`. The `pbftid`s of PBFT leaders are multiples of 4.

To faclitate publickey exchange, we let all servers connect with each other, and the client connect to all servers no matter they are PBFT leaders or followers. However, one can modify the code to disconnect a node with servers in other shards and disconnect the client from PBFT follower servers once the initial public key exchange is done. 

A node will initiate  an outbound connnection to another node if the latter's IP address is provided in the former's `bitcoin.conf` file, i.e. `connect=<IP>:8330`. 

An example of all 9 `bitcoin.conf` files is given under `<git_repo_folder>/STP_files/2shards_conf`. Make sure you replace the IP addresses in those files with the IP addresses of you machines. Then put these 9 files to the correct machine to replace the original `bitcoin.conf`. 

Start `bitcoind` on all servers and then the client. After a while, you can check if servers have already connected to each other using the `getpeerinfo` RPC, i.e., `<git_repo_folder>/src/bitcoin-cli --datadir=blocks_600999 getpeerinfo`. You should seeevery PBFT followers and the client have 8 peers each, and the PBFT leaders have 7 peers. 


5. Prepare the Client
Because the client is going to send transactions in Bitcoin blocks starting from height 601000, the client need to download more blocks from the public Bitcoin network. Redo step 2 but set `synctoheight = 601120` in the `bitcoin.conf` because we will use at most 120 blocks. Rename the `blocks_600999` folder to `blocks_601120`.

The client tracks transaction dependency so that we can avoid aborting tranactions. Transaction dependency analysis is done offline, and the results are saved in a folder called `dependency_file/`. Manually create this folder at the path where you are going to run `bitcoind`. Follow the steps below.
- start the STP-client-version bitcoind: `<STP-client-version bitcoind> --datadir=blocks_601120`
- generate dependency graph: `<STP-client-version bitcoin-cli> --datadir=blocks_601120 gendependgraph 601120`.
- shut down the `bitcoind`


## 7. Test STP
### Prepare Placement Result Files (only for offline STP)
Replace `blocks_601120/chainstate` with `chainstate_withAffinity_2shard`, so that the client knows shard affinities of all UTXOs to run STP for transactions in blocks 601000 to 601120.

Note that STP is an online algorigthm. We provide steps for offline STP only for evaluation purpose when client and server have very slow disk access that affects online evaluation results. If client-side disk access is too slow, the client cannot send transactions very fast because it needs to access UTXO affinities on disk to calculate the output shard.  

The placement result will be saved in folder `/home/l27ren/shard_info_files/2committees/`. Make sure to change this folder by editing line 159 of file `src/tx_placement/tx_placer.h` on the STP-client-version code branch (`omniledger_smartPlace_client`)
to match your local storage path. Then recompile the code.

Follow the steps below to generate STP results:
- start the STP-client-version bitcoind: `<STP-client-version bitcoind> --datadir=blocks_601120`
- generate dependency graph: `<STP-client-version bitcoin-cli> --datadir=blocks_601120 genshardinfofile 601000 601120`.
- after the `genshardinfofile` returns, shutdown the `bitcoind`.
- check the folder you specified in the source code. There should be files like `601000_shardinfo.out`.

### Create Output File Folders
The client will write throughput and latency results to folder `/home/l27ren/tx_placement`, so change the path according to you local storage by modifying line 120~122 of file `src/pbft/pbft.cpp` and recompile the code. Also, please create the folder manually.

### Warm up Memory Page Cache
Before running formal tests, we must run tests for 3 times to warm up memory page cache. The steps for running formal tests and warm-up tests are the same: 
- start all 8 servers, and then the client. 
- wait for a while for clients and servers to connect (check `getpeerinfo` result). 
- let the client send transactions to servers using an RPC on the client machine: `<STP-client-version bitcoin-cli> --datadir=blocks_601120 sendtxinblocks 601000 601020`. Note that we test 2 shards with 20 blocks.

### Run test
See the steps in warming up memory page cache.

6. Test Hashing Placement
Same as [Test STP](#7-test-stp), but without preparing placement result files. Also, use HP-sever-verison and HP-client-version `bitcoind`s (See the corresponding branch in section [Source Code](#source-code)).
