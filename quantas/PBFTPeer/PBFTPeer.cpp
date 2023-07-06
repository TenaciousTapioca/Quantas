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
	int PBFTPeer::numOfCrashes = 0;
	int PBFTPeer::timeout = 0;

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
		checkInStrm();

		if (!paused && !crashed)
			checkContents();

		// Check for view-change/new-view msgs and increment timer
		if (id() == leaderId && crashed) { return; }

		if (!crashed && !paused && timer >= timeout) { // send view-change
			paused = true;
			submitViewChange(messageType::view_change);
		} else if ((receivedMessages.size() - 1) == sequenceNum) { // go to new view
			if (receivedMessages[sequenceNum].back().type == messageType::new_view) {
				paused = false;
				viewNum += 1;
				leaderId = candidateId;
				candidateId = (leaderId + 1) % (neighbors().size() + 1);
				timer = 0;
				receivedMessages.pop_back();
			}
		} else {
			++timer;
		}

		// Check if 2f valid view-changes for view(v+1) were received
		if (id() == candidateId && (receivedMessages.size() - 1) == sequenceNum) {
			int count = 0;
			for (int i = 0; i < receivedMessages[sequenceNum].size(); ++i) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];
				if (message.type == messageType::view_change && message.viewNum == (viewNum + 1)) {
					++count;
				}
			}

			// Upon 2f view-changes, leader of view(v+1) broadcasts a new-view
			if (count > (neighbors().size() * 2/ 3)) {
				submitViewChange(messageType::new_view);
				submitTrans(currentTransaction);
			}
		}
		
	}

	void PBFTPeer::initParameters(const vector<Peer<PBFTPeerMessage>*>& _peers, json parameters) {
		if (parameters.contains("numOfCrashes")) {
			numOfCrashes = parameters["numOfCrashes"];
		}
		if (parameters.contains("timeout")) {
			timeout = parameters["timeout"];
			--timeout;
		}

		const vector<PBFTPeer*> peers = reinterpret_cast<vector<PBFTPeer*> const&>(_peers);

		int count = 0;
		const int leader = peers[0]->viewNum % peers.size();
		const int candidate = (peers[0]->viewNum + 1) % peers.size();
		for (int i = 0; i < peers.size(); ++i) {
			peers[i]->leaderId = leader;
			peers[i]->candidateId = candidate;

			// Set the first "numOfCrashes" peers to be crashed
			if (count < numOfCrashes) {
				peers[i]->crashed = true;
				++count;
			}
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
	}

	void PBFTPeer::checkInStrm() {
		while (!inStreamEmpty()) {
			Packet<PBFTPeerMessage> newMsg = popInStream();
			
			if (newMsg.getMessage().type == messageType::trans) {
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
		if (id() == leaderId && status == statusType::pre_prepare) {
			for (int i = 0; i < transactions.size(); i++) {
				bool skip = false;
				for (int j = 0; j < confirmedTrans.size(); j++) {
					if (transactions[i].trans == confirmedTrans[j].trans) {
						skip = true;
						break;
					}
				}
				if (!skip) {
					status = statusType::prepare;
					PBFTPeerMessage message = transactions[i];
					message.type = messageType::pre_prepare;
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
		} else if (status == statusType::pre_prepare && receivedMessages.size() >= sequenceNum + 1) {
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];
				if (message.type == messageType::pre_prepare) {
					status = statusType::prepare;
					PBFTPeerMessage newMsg = message;
					newMsg.type = messageType::prepare;
					newMsg.Id = id();
					broadcast(newMsg);
					receivedMessages[sequenceNum].push_back(newMsg);
				}
			}
		}

		if (status == statusType::prepare) {
			int count = 0;
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];		
				if (message.type == messageType::prepare) {
					count++;
				}
			}
			if (count >= (neighbors().size() * 2 / 3)) {
				status = statusType::commit;
				PBFTPeerMessage newMsg = receivedMessages[sequenceNum][0];
				newMsg.type = messageType::commit;
				newMsg.Id = id();
				broadcast(newMsg);
				receivedMessages[sequenceNum].push_back(newMsg);
			}
		}

		if (status == statusType::commit) {
			int count = 0;
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];
				if (message.type == messageType::commit) {
					count++;
				}
			}
			if (count > (neighbors().size() * 2 / 3)) {
				status = statusType::pre_prepare;
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
		message.type = messageType::trans;
		message.trans = tranID;
		message.Id = id();
		message.roundSubmitted = getRound();
		broadcast(message);
		transactions.push_back(message);
		currentTransaction++;
	}

	// submits either "view_change" or "new_view"
	void PBFTPeer::submitViewChange(messageType type) {
		PBFTPeerMessage message;
		message.type = type;
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
