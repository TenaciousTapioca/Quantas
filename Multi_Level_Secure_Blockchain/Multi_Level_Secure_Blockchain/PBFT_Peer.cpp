//
//  PBFT_Peer.cpp
//  Multi_Level_Secure_Blockchain
//
//  Created by Kendric Hood on 3/19/19.
//  Copyright © 2019 Kent State University. All rights reserved.
//

#include <limits>
#include "PBFT_Peer.hpp"

PBFT_Peer::PBFT_Peer(std::string id) : Peer<PBFT_Message>(id){
    _requestLog = std::vector<PBFT_Message>();
    _prePrepareLog = std::vector<PBFT_Message>();
    _prepareLog = std::vector<PBFT_Message>();
    _commitLog = std::vector<PBFT_Message>();
    _ledger = std::vector<PBFT_Message>();
    
    _faultUpperBound = 0;
    _currentRound = 0;
    
    _primary = nullptr;
    _currentPhase = IDEAL;
    _currentView = 0;
    _currentRequest = PBFT_Message();
    _currentRequestResult = 0;
}

PBFT_Peer::PBFT_Peer(std::string id, double fault) : Peer<PBFT_Message>(id){
    _requestLog = std::vector<PBFT_Message>();
    _prePrepareLog = std::vector<PBFT_Message>();
    _prepareLog = std::vector<PBFT_Message>();
    _commitLog = std::vector<PBFT_Message>();
    _ledger = std::vector<PBFT_Message>();
    
    _faultUpperBound = fault;
    _currentRound = 0;
    
    _primary = nullptr;
    _currentPhase = IDEAL;
    _currentView = 0;
    _currentRequest = PBFT_Message();
    _currentRequestResult = 0;
}

PBFT_Peer::PBFT_Peer(std::string id, double fault, int round) : Peer<PBFT_Message>(id){
    _requestLog = std::vector<PBFT_Message>();
    _prePrepareLog = std::vector<PBFT_Message>();
    _prepareLog = std::vector<PBFT_Message>();
    _commitLog = std::vector<PBFT_Message>();
    _ledger = std::vector<PBFT_Message>();
    
    _faultUpperBound = fault;
    _currentRound = round;
    
    _primary = nullptr;
    _currentPhase = IDEAL;
    _currentView = 0;
    _currentRequest = PBFT_Message();
    _currentRequestResult = 0;
}

PBFT_Peer::PBFT_Peer(const PBFT_Peer &rhs) : Peer<PBFT_Message>(rhs){
    _requestLog = rhs._requestLog;
    _prePrepareLog = rhs._prePrepareLog;
    _prepareLog = rhs._prepareLog;
    _commitLog = rhs._commitLog;
    _ledger = rhs._ledger;
    
    _faultUpperBound = rhs._faultUpperBound;
    _currentRound = rhs._currentRound;
    
    _primary = rhs._primary;
    _currentPhase = rhs._currentPhase;
    _currentView = rhs._currentView;
    _currentRequest = rhs._currentRequest;
    _currentRequestResult = rhs._currentRequestResult;
}

PBFT_Peer& PBFT_Peer::operator=(const PBFT_Peer &rhs){
    
    Peer<PBFT_Message>::operator=(rhs);
    
    _requestLog = rhs._requestLog;
    _prePrepareLog = rhs._prePrepareLog;
    _prepareLog = rhs._prepareLog;
    _commitLog = rhs._commitLog;
    _ledger = rhs._ledger;
    
    _faultUpperBound = rhs._faultUpperBound;
    _currentRound = rhs._currentRound;
    
    _primary = rhs._primary;
    _currentPhase = rhs._currentPhase;
    _currentView = rhs._currentView;
    _currentRequest = rhs._currentRequest;
    _currentRequestResult = rhs._currentRequestResult;
    
    return *this;
}

void PBFT_Peer::collectMessages(){
    while(!_inStream.empty()){
        if(_inStream.front().getMessage().type == REQUEST && _primary->id() == _id){
            _requestLog.push_back(_inStream.front().getMessage());
            _inStream.erase(_inStream.begin());
            
        }else if(_inStream.front().getMessage().phase == PRE_PREPARE){
            _prePrepareLog.push_back(_inStream.front().getMessage());
            _inStream.erase(_inStream.begin());
            
        }else if(_inStream.front().getMessage().phase == PREPARE){
            _prepareLog.push_back(_inStream.front().getMessage());
            _inStream.erase(_inStream.begin());
            
        }else if(_inStream.front().getMessage().phase == COMMIT){
            _commitLog.push_back(_inStream.front().getMessage());
            _inStream.erase(_inStream.begin());
            
        }
    }
}

void PBFT_Peer::prePrepare(){
    if(_currentPhase != IDEAL ||
       _requestLog.empty() ||
       _primary->id() != _id){
        return;
    }
    PBFT_Message request = _requestLog.front();
    _requestLog.erase(_requestLog.begin());
    
    request.sequenceNumber = (int)_ledger.size() + 1;
    request.phase = PRE_PREPARE;
    request.type = REPLY;
    _currentPhase = PREPARE_WAIT;
    _currentRequest = request;
    _currentRequestResult = executeQuery(request);
    braodcast(request);
}

void PBFT_Peer::prepare(){
    if(_currentPhase != IDEAL){
        return;
    }
    PBFT_Message prePrepareMesg;
    if(!_prePrepareLog.empty()){
        prePrepareMesg = _prePrepareLog.front();
        _prePrepareLog.erase(_prePrepareLog.begin());
    }else{
        return;
    }
    if(!isVailedRequest(prePrepareMesg)){
        return;
    }
    PBFT_Message prepareMsg = prePrepareMesg;
    prepareMsg.creator_id = _id;
    prepareMsg.view = _currentView;
    prepareMsg.type = REPLY;
    prepareMsg.round = _currentRound;
    prepareMsg.phase = PREPARE;
    braodcast(prepareMsg);
    _currentPhase = PREPARE_WAIT;
    _currentRequest = prePrepareMesg;
}

void PBFT_Peer::waitPrepare(){
    if(_currentPhase != PREPARE_WAIT){
        return;
    }
    
    int numberOfPrepareMsg = 0;
    for(int i = 0; i < _prepareLog.size(); i++){
        if(_prepareLog[i].sequenceNumber == _currentRequest.sequenceNumber
           && _prepareLog[i].view == _currentView){
            numberOfPrepareMsg++;
        }
    }
    // _neighbors.size() + 1 is neighbors plus this peer
    if(numberOfPrepareMsg > (ceil((_neighbors.size() + 1) * _faultUpperBound) + 1)){
        _currentPhase = COMMIT;
    }
}

void PBFT_Peer::commit(){
    if(_currentPhase != COMMIT){
        return;
    }
    
    PBFT_Message commitMsg = _currentRequest;
    _currentRequestResult = executeQuery(_currentRequest);
    
    commitMsg.phase = COMMIT;
    commitMsg.creator_id = _id;
    commitMsg.type = REPLY;
    commitMsg.result = _currentRequestResult;
    commitMsg.round = _currentRound;
    braodcast(commitMsg);
    
    _currentPhase = COMMIT_WAIT;
}

void PBFT_Peer::waitCommit(){
    if(_currentPhase != COMMIT_WAIT){
        return;
    }
    
    int numberOfCommitMsg = 0;
    for(int i = 0; i < _commitLog.size(); i++){
        if(_commitLog[i].sequenceNumber == _currentRequest.sequenceNumber
           && _commitLog[i].view == _currentView
           && _commitLog[i].result == _currentRequestResult){
            
            numberOfCommitMsg++;
        }
    }
    if(numberOfCommitMsg > ceil((_neighbors.size() + 1) * _faultUpperBound) + 1){
        PBFT_Message reply = _currentRequest;
        reply.round = _currentRound;
        reply.round = _currentRound;
        reply.result = _currentRequestResult;
        reply.type = REPLY;
        reply.phase = COMMIT;
        _ledger.push_back(reply);
        
        Packet<PBFT_Message> replayPck(makePckId());
        replayPck.setSource(_id);
        replayPck.setTarget(reply.client_id);
        replayPck.setBody(reply);
        _outStream.push_back(replayPck);
        _currentPhase = IDEAL; // complete distributed-consensus
        _currentRequestResult = 0;
        _currentRequest = PBFT_Message();
    }
    
}

Peer<PBFT_Message>* PBFT_Peer::findPrimary(const std::vector<Peer<PBFT_Message> *> neighbors){
    
    std::vector<std::string> peerIds = std::vector<std::string>();
    for(int i = 0; i < neighbors.size(); i++){
        peerIds.push_back(neighbors[i]->id());
    }
    peerIds.push_back(_id);
    
    std::sort(peerIds.begin(), peerIds.end());
    std::string primaryId = peerIds[_currentView%peerIds.size()];
    
    if(primaryId == _id){
        return this;
    }
    for(int i = 0; i < neighbors.size(); i++){
        if(primaryId == neighbors[i]->id()){
            return neighbors[i];
        }
    }
    return nullptr;
}

int PBFT_Peer::executeQuery(const PBFT_Message &query){
    switch (query.operation) {
        case ADD:
            return query.operands.first + query.operands.second;
            break;
            
        case SUBTRACT:
            return query.operands.first - query.operands.second;
            break;
            
        default:
            *_log<< "ERROR: invailed request excution"<< std::endl;
            return 0;
            break;
    }
}

bool PBFT_Peer::isVailedRequest(const PBFT_Message &query)const{
    if(query.view != _currentView){
        return false;
    }
    if(query.sequenceNumber <= _ledger.size()){
        return false;
    }
    if(query.phase != PRE_PREPARE){
        return false;
    }
    return true;
}

void PBFT_Peer::braodcast(const PBFT_Message &msg){
    for(int i = 0; i < _neighbors.size(); i++){
        std::string neighborId = _neighbors[i]->id();
        Packet<PBFT_Message> pck(makePckId());
        pck.setSource(_id);
        pck.setTarget(neighborId);
        pck.setBody(msg);
        _outStream.push_back(pck);
    }
}

void PBFT_Peer::preformComputation(){
    if(_primary == nullptr){
        _primary = findPrimary(_neighbors);
    }
    collectMessages(); // sorts messages into there repective logs
    prePrepare();
    prepare();
    waitPrepare();
    commit();
    waitCommit();
    _currentRound++;
}

void PBFT_Peer::makeRequest(){
    if(_primary == nullptr){
        *_log<< "ERROR: makeRequest called with no primary"<< std::endl;
        return;
    }
    
    // create request
    PBFT_Message request;
    request.client_id = _id;
    request.creator_id = _id;
    request.view = _currentView;
    request.type = REQUEST;
    
    bool add = (rand()%2);
    if(add){
        request.operation = ADD;
    }else{
        request.operation = SUBTRACT;
    }
    
    request.operands = std::pair<int, int>();
    request.operands.first = (rand()%100)+1;
    request.operands.second = (rand()%100)+1;
    
    request.round = _currentRound;
    request.phase = IDEAL;
    request.sequenceNumber = -1;
    request.result = 0;
    
    // create packet for request
    Packet<PBFT_Message> pck(makePckId());
    pck.setSource(_id);
    pck.setTarget(_primary->id());
    pck.setBody(request);
    _outStream.push_back(pck);
}

std::ostream& PBFT_Peer::printTo(std::ostream &out)const{
    Peer<PBFT_Message>::printTo(out);
    out<< std::left;
    
    std::string primaryId;
    if(_primary == nullptr){
        primaryId = "NO PRIMARY";
    }else{
        primaryId = _primary->id();
    }
    
    out<< "\t"<< "Settings:"<< std::endl;
    out<< "\t"<< std::setw(LOG_WIDTH)<< "Fault Upper Bound"<< std::endl;
    out<< "\t"<< std::setw(LOG_WIDTH)<< _faultUpperBound<< std::endl;
    
    out<< "\t"<< "Current State:"<< std::endl;
    out<< "\t"<< std::setw(LOG_WIDTH)<< "Round"<< std::setw(LOG_WIDTH)<< "Current Phase"<< std::setw(LOG_WIDTH)<< "Current View"<< std::setw(LOG_WIDTH)<< "Primary ID"<< std::setw(LOG_WIDTH)<< "Current Request Client ID"<< std::setw(LOG_WIDTH)<< "Current Request Result"<< std::endl;
    out<< "\t"<< std::setw(LOG_WIDTH)<< _currentRound<< std::setw(LOG_WIDTH)<< _currentPhase<< std::setw(LOG_WIDTH)<< _currentView<< std::setw(LOG_WIDTH)<< primaryId<< std::setw(LOG_WIDTH)<< _currentRequest.client_id<< std::setw(LOG_WIDTH)<< _currentRequestResult<< std::endl;
    
    out<< "\t"<< std::setw(LOG_WIDTH)<< "Request Log"<< std::setw(LOG_WIDTH)<< "Pre-Prepare Log Size"<< std::setw(LOG_WIDTH)<< "Prepare Log Size"<< std::setw(LOG_WIDTH)<< "Commit Log Size"<< std::setw(LOG_WIDTH)<< "Ledger Size"<<  std::endl;
    out<< "\t"<< std::setw(LOG_WIDTH)<< _requestLog.size()<< std::setw(LOG_WIDTH)<< _prePrepareLog.size()<< std::setw(LOG_WIDTH)<< _prepareLog.size()<< std::setw(LOG_WIDTH)<< _commitLog.size()<< std::setw(LOG_WIDTH)<< _ledger.size()<< std::endl <<std::endl;
    
    return out;
}
