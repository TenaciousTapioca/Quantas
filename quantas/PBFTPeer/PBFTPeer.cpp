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
	int PBFTPeer::maxCrashes = 0;
	int PBFTPeer::numOfCrashes = 0;
	int PBFTPeer::timeout = 0;

	PBFTPeer::~PBFTPeer() {
	}

	PBFTPeer::PBFTPeer(const PBFTPeer& rhs) : Peer<PBFTPeerMessage>(rhs) {	
	}

	PBFTPeer::PBFTPeer(long id) : Peer(id) {
	}

	void PBFTPeer::performComputation() {
		// randomly crash future leaders
		const int randNum = randMod(100);
		int chance = (getRound() >= 100) ? 100 : getRound();
		if ((id() < maxCrashes) && !crashed && (numOfCrashes < maxCrashes) && (randNum < chance)) {
			crashed = true;
			++numOfCrashes;
		}
		if (crashed) { return; }

		if (id() == leaderId && getRound() == 0) {
			submitTrans(currentTransaction);
		}
		checkInStrm();

		// perform the regular phases if no view-change msgs were sent
		if (viewJumps == 0)
			checkContents();

		// increment timer when no valid pre-prepare msg was processed
		if (!paused) ++timer;

		// calculate the view-change timeout (v+1: 1T, v+2: 2T, . . . , v+k: kT)
		if ((viewJumps > 1) && (view_changeTimeout / timeout) == (viewJumps - 1)) {
			view_changeTimeout += timeout;
		}

		if (timer >= view_changeTimeout) { // send view-change
			timer = 0;
			++viewJumps;
			candidateId = (viewNum + viewJumps) % (neighbors().size() + 1);
			submitViewChange(messageType::view_change);
		} else if ((receivedMessages.size() - 1) == sequenceNum) { // go to new view
			PBFTPeerMessage message = receivedMessages[sequenceNum].back();
			if (message.type == messageType::new_view) {
				timer = 0;
				viewJumps = 0;
				viewNum = message.viewNum;
				view_changeTimeout = timeout;
				leaderId = (viewNum) % (neighbors().size() + 1);
				candidateId = (viewNum + 1) % (neighbors().size() + 1);
				receivedMessages.pop_back();
			}

			if ((id() == candidateId) && (receivedMessages.size() - 1) == sequenceNum) {
				// Check if 2f+1 valid view-changes for a greater view were received
				int count = 0;
				for (int i = 0; i < receivedMessages[sequenceNum].size(); ++i) {
					PBFTPeerMessage message = receivedMessages[sequenceNum][i];
					if (message.type == messageType::view_change && message.viewNum == (viewNum + viewJumps)) {
						++count;
					}
				}

				// Upon 2f+1 view-changes, leader of the next view broadcasts a new-view
				if (count >= 2 * (neighbors().size() / 3) + 1) {
					submitViewChange(messageType::new_view);
					submitTrans(currentTransaction);
				}
			}
		}
	}

	void PBFTPeer::initParameters(const vector<Peer<PBFTPeerMessage>*>& _peers, json parameters) {
		if (parameters.contains("maxCrashes")) {
			maxCrashes = parameters["maxCrashes"];
		}
		if (parameters.contains("timeout")) {
			timeout = parameters["timeout"];
		}

		const vector<PBFTPeer*> peers = reinterpret_cast<vector<PBFTPeer*> const&>(_peers);

		for (int i = 0; i < peers.size(); ++i) {
			peers[i]->view_changeTimeout = timeout;
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
			paused = true;	// stop the timer to prevent view-changes during regular phases
			timer = 0;
			int count = 0;
			for (int i = 0; i < receivedMessages[sequenceNum].size(); i++) {
				PBFTPeerMessage message = receivedMessages[sequenceNum][i];		
				if (message.type == messageType::prepare) {
					count++;
				}
			}
			if (count >= 2 * (neighbors().size() / 3)) {  // primary does not send a prepare msg
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
			if (count >= 2 * (neighbors().size() / 3) + 1) {
				status = statusType::pre_prepare;
				paused = false;
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
		message.viewNum = viewNum + viewJumps;
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