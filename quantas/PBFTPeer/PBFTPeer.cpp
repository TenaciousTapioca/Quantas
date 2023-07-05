/*
Copyright 2022
This file is part of QUANTAS.
QUANTAS is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
QUANTAS is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with QUANTAS. If not, see <https://www.gnu.org/licenses/>.
*/

#include <iostream>
#include "PBFTPeer.hpp"

namespace quantas {

	int PBFTPeer::currentTransaction = 1;
	int PBFTPeer::crashRate = 0;
	int PBFTPeer::timeout = 0;
	int PBFTPeer::leaderId = 0;
	int PBFTPeer::candidateId = 0;

	PBFTPeer::~PBFTPeer() {

	}

	PBFTPeer::PBFTPeer(const PBFTPeer& rhs) : Peer<PBFTPeerMessage>(rhs) {
		
	}

	PBFTPeer::PBFTPeer(long id) : Peer(id) {

	}

	void PBFTPeer::performComputation() {
		if (id() == leaderId && getRound() == 0 && !crashed) {
			submitTrans(currentTransaction);
		}
		if (true)
			checkInStrm();

		if (!paused && !crashed)
			checkContents();

	}

	void PBFTPeer::initParameters(const vector<Peer<PBFTPeerMessage>*>& _peers, json parameters) {
		if (parameters.contains("crashRate")) {
			crashRate = parameters["crashRate"];
		}
		if (parameters.contains("timeout")) {
			timeout = parameters["timeout"];
			--timeout;
		}

		const vector<PBFTPeer*> peers = reinterpret_cast<vector<PBFTPeer*> const&>(_peers);
		leaderId = viewNum % peers.size();
		candidateId = (viewNum + 1) % peers.size();
		
		/* // Randomly select peers to be crashed
		int count = 0;
		while (count < crashRate) {
			const int randNum = randMod(peers.size());
			if (!peers[randNum]->crashed) {
				peers[randNum]->crashed = true;
				//std::cout << "Peer[" << randNum << "] is set as crashed\n";
				++count;
			}
		}
		*/
		
		for (int i = 0; i < crashRate; ++i) {
			peers[i]->crashed = true;
		}
		
	}

	void PBFTPeer::endOfRound(const vector<Peer<PBFTPeerMessage>*>& _peers) {
		const vector<PBFTPeer*> peers = reinterpret_cast<vector<PBFTPeer*> const&>(_peers);
		int length = peers[0]->confirmedTrans.size();
        for (int i = 1; i < peers.size(); ++i) {
            if (peers[i]->confirmedTrans.size() > length) {
                length = peers[i]->confirmedTrans.size();
            }
        }
        LogWriter::instance()->data["test"][LogWriter::instance()->getTest()]["throughput"].push_back(length);
	
		// Check for view-changes/new-views or increment timer
		for (int i = 0; i < peers.size(); ++i) {
			PBFTPeer* peer = peers[i];
			if (peer->id() == leaderId && peer->crashed) { continue; }

			if (!peer->crashed && !peer->paused && peer->timer >= timeout) {  // send view-change
				//std::cout << "Peer[" << peer->id() << "] sent view-change msg\n";
				peer->paused = true;
				peer->submitViewChange(std::string("view-change"));
			} else if ((peer->receivedMessages.size() - 1) == peer->sequenceNum) {  // go to new view
				if (peer->receivedMessages[peer->sequenceNum].back().messageType == "new-view") {
					//std::cout << "Peer[" << peer->id() << "] received new-view\n";
					peer->paused = false;
					peer->viewNum += 1;
					peer->timer = 0;
					peer->receivedMessages.pop_back();
				}
			} else {
				++(peer->timer);
			}

			// Check if 2f valid view-changes to view v+1 are received
			if (peer->id() == candidateId && (peer->receivedMessages.size() - 1) == peer->sequenceNum) {
				int count = 0;
				for (int j = 0; j < peer->receivedMessages[peer->sequenceNum].size(); ++j) {
					PBFTPeerMessage message = peer->receivedMessages[peer->sequenceNum][j];
					if (message.messageType == "view-change" && message.viewNum == (peer->viewNum + 1)) {
						++count;
					}
				}

				// Upon 2f view-changes, (v+1) leader sends out new-view
				if (count > (neighbors().size() * 2 / 3)) {
					peer->submitViewChange(std::string("new-view"));
					//std::cout << "\tPeer[" << peer->id() << "] received " << count << " view-change msgs for view " << peer->viewNum << " @ seqNum = " << peer->sequenceNum << "\n";
					leaderId = candidateId;
					candidateId = (leaderId + 1) % peers.size();
					peer->submitTrans(currentTransaction);
					break;
				}
			}
		}
	}

	void PBFTPeer::checkInStrm() {
		while (!inStreamEmpty()) {
			Packet<PBFTPeerMessage> newMsg = popInStream();
			
			if (newMsg.getMessage().messageType == "trans") {
				transactions.push_back(newMsg.getMessage());
			}
			else {
				while (receivedMessages.size() < newMsg.getMessage().sequenceNum + 1) {
					receivedMessages.push_back(vector<PBFTPeerMessage>());
				}
				receivedMessages[newMsg.getMessage().sequenceNum].push_back(newMsg.getMessage());
			}
		}
	}
	void PBFTPeer::checkContents() {
		if (id() == leaderId && status == "pre-prepare") {
			//std::cout << "Peer[" << id() << "] sent(" << sequenceNum << ")\n";
			for (int i = 0; i < transactions.size(); i++) {
				bool skip = false;
				for (int j = 0; j < confirmedTrans.size(); j++) {
					if (transactions[i].trans == confirmedTrans[j].trans) {
						skip = true;
						break;
					}
				}
				if (!skip) {
					status = "prepare";
					PBFTPeerMessage message = transactions[i];
					message.messageType = "pre-prepare";
					message.Id = id();
					message.sequenceNum = sequenceNum;
					message.viewNum = viewNum;
					broadcast(message);
					if (receivedMessages.size() < sequenceNum + 1) {
						receivedMessages.push_back(vector<PBFTPeerMessage>());
					}
					receivedMessages[sequenceNum].push_back(message);
					break;
				}
			}
		} else if (status == "pre-prepare" && receivedMessages.size() >= sequenceNum + 1) {
            //std::cout << "Peer[" << id() << "] received pre-prepare(" << sequenceNum << "); " << receivedMessages[sequenceNum].size() << " msgs\n";
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];
				if (message.messageType == "pre-prepare") {
					status = "prepare";
					PBFTPeerMessage newMsg = message;
					newMsg.messageType = "prepare";
					newMsg.Id = id();
					broadcast(newMsg);
					receivedMessages[sequenceNum].push_back(newMsg);
				}
			}
		}

		if (status == "prepare") {
			int count = 0;
            //std::cout << "Peer[" << id() << "] received prepare(" << sequenceNum << "); " << receivedMessages[sequenceNum].size() << " msgs\n";
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];
				if (message.messageType == "prepare") {
					count++;
				}
			}
			if (count > (neighbors().size() * 2 / 3)) {
				status = "commit";
				PBFTPeerMessage newMsg = receivedMessages[sequenceNum][0];
				newMsg.messageType = "commit";
				newMsg.Id = id();
				broadcast(newMsg);
				receivedMessages[sequenceNum].push_back(newMsg);
			}
		}

		if (status == "commit") {
			int count = 0;
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];
				if (message.messageType == "commit") {
					count++;
				}
			}
			if (count > (neighbors().size() * 2 / 3)) {
				//std::cout << "Peer[" << id() << "] received commit(" << sequenceNum << ") " << count  << " times\n";
				status = "pre-prepare";
				timer = 0;
				confirmedTrans.push_back(receivedMessages[sequenceNum][0]);
				latency += getRound() - receivedMessages[sequenceNum][0].roundSubmitted;
				sequenceNum++;
				if (id() == leaderId) {
					submitTrans(currentTransaction);
				}
				checkContents();
			}
		}
	}

	void PBFTPeer::submitTrans(int tranID) {
		PBFTPeerMessage message;
		message.messageType = "trans";
		message.trans = tranID;
		message.Id = id();
		message.roundSubmitted = getRound();
		broadcast(message);
		transactions.push_back(message);
		currentTransaction++;
	}

	// submits either "view-change" or "new-view"
	void PBFTPeer::submitViewChange(std::string messageType) {
		PBFTPeerMessage message;
		message.messageType = messageType;
		message.viewNum = viewNum + 1;
		message.sequenceNum = sequenceNum;
		message.Id = id();
		message.roundSubmitted = getRound();
		while (receivedMessages.size() < sequenceNum + 1) {
					receivedMessages.push_back(vector<PBFTPeerMessage>());
		}
		receivedMessages[sequenceNum].push_back(message);
		broadcast(message);
	}

	ostream& PBFTPeer::printTo(ostream& out)const {
		Peer<PBFTPeerMessage>::printTo(out);

		out << id() << endl;
		out << "counter:" << getRound() << endl;

		return out;
	}

	ostream& operator<< (ostream& out, const PBFTPeer& peer) {
		peer.printTo(out);
		return out;
	}

	Simulation<quantas::PBFTPeerMessage, quantas::PBFTPeer>* generateSim() {
        
        Simulation<quantas::PBFTPeerMessage, quantas::PBFTPeer>* sim = new Simulation<quantas::PBFTPeerMessage, quantas::PBFTPeer>;
        return sim;
    }
}
