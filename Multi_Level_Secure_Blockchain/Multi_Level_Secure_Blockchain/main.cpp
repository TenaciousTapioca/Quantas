//
//  main.cpp
//  Multi_Level_Secure_Blockchain
//
//  Created by Kendric Hood on 3/8/19.
//  Copyright © 2019 Kent State University. All rights reserved.
//

#include <iostream>
#include <fstream>
#include <set>
#include "Blockchain.hpp"
#include "Peer.hpp"
#include "ExamplePeer.hpp"
#include "PBFT_Peer.hpp"
#include "syncBFT_Peer.hpp"
#include "Network.hpp"
#include "bCoin_Peer.hpp"
#include "PBFTPeer_Sharded.hpp"
#include "BGSReferenceCommittee.hpp"
#include <iostream>
#include <chrono>
#include <random>


const int peerCount = 10;
const int blockChainLength = 100;
Blockchain *blockchain;
int syncBFT_Peer::peerCount = 3;
int shuffleByzantineInterval = 0;
std::ofstream progress;

// util functions
void buildInitialChain(std::vector<std::string>);
std::set<std::string> getPeersForConsensus(int);
int getMaxLedgerFromBGS_PBFTGroup(const std::vector<PBFTPeer_Sharded*>&);
int getMinLedgerFromBGS_PBFTGroup(const std::vector<PBFTPeer_Sharded*>&);
int sumMessagesSentBGS(const BGSReferenceCommittee&);

void Example();
void PBFT(std::ofstream &out,int);
void syncBFT(std::ofstream &,int );
void bitcoin(std::ofstream &,int );
void bsg(std::ofstream &out,int);

int main(int argc, const char * argv[]) {
    srand((float)time(NULL));
    
    std::string algorithm = argv[1];
    std::string filePath = argv[2];

    if(algorithm == "example"){
        Example();
    }
    else if (algorithm== "pbft"){
        for(int delay = 2; delay < 51; delay = delay + 10){
            std::cout<< "Delay:"+std::to_string(delay)<< std::endl;
            std::ofstream out;
            out.open(filePath + "/PBFT_Delay"+std::to_string(delay) + ".log");
            for(int run = 0; run < 5; run++){
                std::cout<< "run:"<<run<<std::endl;
                PBFT(out,delay);
            }
            out.close();
        }

    }else if (algorithm == "syncBFT") {
        std::ofstream out;
        for(int delay = 1;delay<2; delay+=10){
            std::cout<< "Start with Delay "+std::to_string(delay)<< std::endl;
            for(int run = 0; run < 2; run++){
                syncBFT(out, delay);
            }

        }
    }else if (algorithm == "bgs") {
        std::cout<< "BGS"<<std::endl;
        std::ofstream out;
        for(int delay = 1; delay < 50; delay = delay + 10){
            progress<< "Delay:"+std::to_string(delay)<< std::endl;
            std::ofstream out;
            out.open(filePath + "/BGS_Delay"+std::to_string(delay) + ".csv");
            progress.open(filePath + "/progress.txt");
            for(int run = 0; run < 10; run++){
                progress<< "run:"<<run<<std::endl;
                bsg(out,delay);
            }
            out.close();
            if(delay == 1){
                delay = 0;
            }
        }
        out.close();
    }else if (algorithm == "bitcoin") {
        std::ofstream out;
        bitcoin(out, 1);
    }
    
    return 0;
}

void PBFT(std::ofstream &out,int avgDelay){
    
    Network<PBFT_Message, PBFT_Peer> system;
    system.setToPoisson();
    system.setLog(out);
    system.setToPoisson();
    system.setAvgDelay(avgDelay);
    system.initNetwork(1024);
    for(int i = 0; i < system.size(); i++){
        system[i]->setFaultTolerance(0.3);
        system[i]->init();
    }
    
    int numberOfRequests = 0;
    for(int i =-1; i < 1000; i++){
        if(i%100 == 0 && i != 0){
            progress<< std::endl;
        }
        progress<< "."<< std::flush;
        
        int randIndex = rand()%system.size();
        while(system[randIndex]->isPrimary()){
            randIndex = rand()%system.size();
        }
        system.makeRequest(randIndex);
        numberOfRequests++;
        
        system.receive();
        system.preformComputation();
        system.transmit();
        //system.log();
    }

    int min = (int)system[0]->getLedger().size();
    int max = (int)system[0]->getLedger().size();
    int totalMessages = system[0]->getMessageCount();
    for(int i = 0; i < system.size(); i++){
        //out<< "Peer ID:"<< system[i].id() << " Ledger Size:"<< system[i].getLedger().size()<< std::endl;
        if(system[i]->getLedger().size() < min){
            min = (int)system[i]->getLedger().size();
        }
        if(system[i]->getLedger().size() > max){
            max = (int)system[i]->getLedger().size();
        }
        totalMessages += system[i]->getMessageCount();
    }
    out<< "Min Ledger:,"<< min<< std::endl;
    out<< "Max Ledger:,"<< max<< std::endl;
    out<< "Total Messages:,"<< totalMessages<< std::endl;
    out<< "Total Request:,"<< numberOfRequests<<std::endl;
    std::cout<< std::endl;
}

void syncBFT(std::ofstream &out,int maxDelay){
    srand(time(nullptr));
    int shuffleByzantineInterval = 50;
    int shuffleByzantineCount = 1;

    syncBFT_Peer::changeLeader = true;
    syncBFT_Peer::leaderIdCandidates = {};
    syncBFT_Peer::leaderId = "";
    syncBFT_Peer::syncBFTsystemState = 0;

    Network<syncBFTmessage, syncBFT_Peer> n;
    Network<PBFT_Message, PBFT_Peer> system;

    n.setMaxDelay(maxDelay);
    n.setToRandom();
    n.initNetwork(syncBFT_Peer::peerCount);

    int consensusSize = 0;

    vector<string> byzantinePeers;
    while(byzantinePeers.size()<(syncBFT_Peer::peerCount-1)/2) {
        int index = rand()%syncBFT_Peer::peerCount;
        if (std::find(byzantinePeers.begin(),byzantinePeers.end(),n[index]->id())!= byzantinePeers.end()){
        }else{
            n[index]->setByzantineFlag(true);
            byzantinePeers.push_back(n[index]->id());
        }
    }

    std::cerr<<"Byzantine Peers are ";
    for(auto &peer: byzantinePeers){
        std::cerr<<peer<<" ";
    }

    int lastConsensusAt = 0;

    //start with the first peer as leader

    bool shuffleByzantinePeers = false;
    syncBFT_Peer::txToConsensus = "InitialTx";

    for(int i =1; i<1000; i++){

        if(i%shuffleByzantineInterval == 0){
            std::cerr<<"Iteration "<<i<<std::endl;
            shuffleByzantinePeers = true;
        }

        if( i%5 == 0 ){
            n.makeRequest(rand()%n.size());
        }
        if(syncBFT_Peer::txToConsensus.empty() && !syncBFT_Peer::txQueue.empty()){
            syncBFT_Peer::txToConsensus = syncBFT_Peer::txQueue.front();
            syncBFT_Peer::txQueue.pop();
        }

        n.receive();
        if(!syncBFT_Peer::txToConsensus.empty()){

            n.preformComputation();

            if(syncBFT_Peer::syncBFTsystemState == 3){
                if(shuffleByzantinePeers){
                    n.shuffleByzantines (shuffleByzantineCount);
                    shuffleByzantinePeers = false;
                }
                //check if all peers terminated
                bool consensus = true;
                for(int peerId = 0; peerId<n.size(); peerId++ ) {
                    if (!n[peerId]->getTerminationFlag()) {
                        //the leader does not need to terminate
                        if (n[peerId]->id() == syncBFT_Peer::getLeaderId()) {
                            continue;
                        }
                        consensus = false;
                        break;
                    }
                }
                if (consensus){
                    consensusSize++;
                    std::cerr<<"++++++++++++++++++++++++++++++++++++++++++Consensus reached at iteration "<<i<<std::endl;
                    //refresh the peers
                    for(int i = 0;i<n.size();i++){
                        n[i]->refreshSyncBFT();

                    }
                    //reset system-wide sync state
                    syncBFT_Peer::syncBFTsystemState = 0;

                    lastConsensusAt = i;
                    continue;
                }
            }

            if((i-lastConsensusAt)%n.maxDelay() ==0){
                //reset sync status of the peers after a notify step is done.
                if(syncBFT_Peer::syncBFTsystemState==3){
                    for(int i = 0; i<n.size();i++){
                        n[i]->setSyncBFTState(0);
                        n[i]->iter++;
                    }
                }
                syncBFT_Peer::incrementSyncBFTsystemState();
            }

        }
        else{
            std::cerr<<"IDLE FOR ITERATION "<<i<<std::endl;
        }

        n.transmit();
    }

    while(!syncBFT_Peer::txQueue.empty()){
        std::cerr<<syncBFT_Peer::txQueue.front()<<std::endl;
        syncBFT_Peer::txQueue.pop();
    }

    std::cerr<<"Shuffled byzantine peers every "<<shuffleByzantineInterval<<"\tMax delay: "<<maxDelay<<"\t Consensus count: "<<n[0]->getBlockchain()->getChainSize()-1<<std::endl;

}

void bitcoin(std::ofstream &out, int avgDelay){
    Network<bCoinMessage, bCoin_Peer> n;
    n.setToPoisson();
    n.setAvgDelay(avgDelay);
    n.setLog(std::cout);
    n.initNetwork(10);

    //mining delays at the beginning
    for(int i = 0; i<n.size(); i++){
        n[i]->setMineNextAt( bCoin_Peer::distribution(bCoin_Peer::generator) );
    }

    for(int i = 1; i<100; i++){
        std::cerr<<"Iteration "<<i<<std::endl;
        n.receive();
        n.preformComputation();
        n.transmit();
    }
    int maxChain = 0;
    for(int i =0;i<n.size();i++){
        if(n[i]->getBlockchain()->getChainSize()>maxChain)
            maxChain = i;
    }
    std::cerr<<"Number of confirmations = "<<n[maxChain]->getBlockchain()->getChainSize()<<std::endl;
}

void Example(){
    Network<ExampleMessage,ExamplePeer> n;
    n.setMaxDelay(1);
    n.initNetwork(2);

    for(int i =0; i < 3; i++){
        std::cout<< "-- STARTING ROUND "<< i<< " --"<<  std::endl;

        n.receive();
        n.preformComputation();
        n.transmit();

        std::cout<< "-- ENDING ROUND "<< i<< " --"<<  std::endl;
    }
    
    ExamplePeer A("A");
    ExamplePeer B("B");

    A.addNeighbor(B, 10);
    B.addNeighbor(A, 5);
    
    std::cout << A<< std::endl;
    std::cout << B<< std::endl;
    
    std::cout<< n<< std::endl;
}

void bsg(std::ofstream &out,int avgDelay){
    BGSReferenceCommittee system = BGSReferenceCommittee();
    system.setGroupSize(16);
    system.setToPoisson();
    system.setAvgDelay(avgDelay);
    system.setLog(out);
    system.initNetwork(1024);
    system.setFaultTolerance(0.3);

    int numberOfRequests = 0;
    for(int i =-1; i < 1000; i++){
        if(i%100 == 0 && i != 0){
           progress<< std::endl;
        }
        progress<< ".";
        
        if(i%5 == 0){
            system.makeRequest();
            numberOfRequests++;
        }
        
        system.receive();
        system.preformComputation();
        system.transmit();
        //system.log();
    }
    int min = 0;
    int max = 0;
    
    for(int id = 0; id < system.numberOfGroups(); id++){
        max += getMaxLedgerFromBGS_PBFTGroup(system.getGroup(id));
    }
    
    for(int id = 0; id < system.numberOfGroups(); id++){
        min += getMinLedgerFromBGS_PBFTGroup(system.getGroup(id));
    }
    
    int totalMessages = sumMessagesSentBGS(system);
    
    out<< "Min Ledger:,"<< min<< std::endl;
    out<< "Max Ledger:,"<< max<< std::endl;
    out<< "Total Messages:,"<< totalMessages<< std::endl;
    out<< "Total Request:,"<< numberOfRequests<<std::endl;
    progress<< std::endl;
}

//
// util functions
//

void buildInitialChain(std::vector<std::string> peerIds) {
    std::cerr << "Building initial chain" << std::endl;
    srand((time(nullptr)));
    Blockchain *preBuiltChain = new Blockchain(true);
    int index = 1;                  //blockchain index starts from 1;
    int blockCount = 1;
    std::string prevHash = "genesisHash";
    
    while (blockCount < blockChainLength) {
        std::set<std::string> publishers;
        
        std::string peerId = peerIds[rand()%peerIds.size()];
        std::string blockHash = std::to_string(index) + "_" + peerId;
        publishers.insert(peerId);
        preBuiltChain->createBlock(index, prevHash, blockHash, publishers);
        prevHash = blockHash;
        index++;
        blockCount++;
    }
    
    std::cerr << "Initial chain build complete." << std::endl;
    std::cerr << "Prebuilt chain: " << std::endl;
    std::cerr << *preBuiltChain << std::endl;
    std::cerr << preBuiltChain->getChainSize() << std::endl;
    blockchain = preBuiltChain;
    
    
}

std::set<std::string> getPeersForConsensus(int securityLevel) {
    int numOfPeers = peerCount / securityLevel;
    std::set<std::string> peersForConsensus;
    int chainSize = blockchain->getChainSize ();
    int i = blockchain->getChainSize() - 1;             //i keeps track of index in the chain
    while (peersForConsensus.size() < numOfPeers) {
        std::string peerId = *(blockchain->getBlockAt(i).getPublishers()).begin();
        peersForConsensus.insert(peerId);
        //random value
        int randVal = rand()%peerCount;
        
        int skip = randVal;
        if ((i - skip) <= 0) {
            i = chainSize - i - skip;
            
        } else
            i = i - skip;
    }
    
    return peersForConsensus;
}

int getMaxLedgerFromBGS_PBFTGroup(const std::vector<PBFTPeer_Sharded*> &group){
    int max = (int)group[0]->getLedger().size();
    for(int i = 0; i < group.size(); i++){
        if(group[i]->getLedger().size() > max){
            max = (int)group[i]->getLedger().size();
        }
    }
    return max;
}

int getMinLedgerFromBGS_PBFTGroup(const std::vector<PBFTPeer_Sharded*> &group){
    int min = (int)group[0]->getLedger().size();
    for(int i = 0; i < group.size(); i++){
        if(group[i]->getLedger().size() < min){
            min = (int)group[i]->getLedger().size();
        }
    }
    return min;
}

int sumMessagesSentBGS(const BGSReferenceCommittee &system){
    int sum = 0;
    for(int i = 0; i < system.size(); i++){
        sum += system[i]->getMessageCount();
    }
    return sum;
}
