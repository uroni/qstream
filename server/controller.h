/**
* Controller. Does the exploration, handles Congestion Control
* and uses the trees constucted by the Tracker to distribute data.
**/

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "../common/types.h"
#include <map>

struct SRtt;

/**
* Data structure to give information about peers to the tracker thread
**/
struct SBest
{
	bool operator<(const SBest &other) const
	{
		return free_msgs<other.free_msgs;
	}
	unsigned int free_msgs;
	unsigned int used_msgs;
	float free_msgs_var;
	unsigned int peer_id;
	unsigned int ip;
	unsigned short port;
	unsigned int id;
	std::map<unsigned int, SRtt> latencies;
	float server_rtt;
	SOCKET s;
};

#include "input.h"
#include "tracker.h"
#include <boost/thread/mutex.hpp>
#include <list>
#include <queue>
#include "../common/socket_functions.h"

class Tracker;

/**
*  Data structure to save information about peers
**/
struct SRtt
{
	float mean;
	float var;
};

/**
* Data structure to save reinforcement learning rewards
**/
struct SActionReward
{
	SActionReward(){ value=0; action=0; }
	SActionReward(int pAction, double pValue){ action=pAction; value=pValue; }
	double value;
	int action;
	std::vector<SActionReward> rewards;
};

/**
* Data structure to save the q-valus
**/
struct SQValues
{
	SQValues(){up=0; stay=0; down=0;}
	SQValues(double pUp, double pDown, double pStay) : up(pUp), down(pDown), stay(pStay) {}
	double up;
	double down;
	double stay;
	std::vector<SActionReward> rewards;
};

/**
* Data structure to save information about a buffer from the input thread
**/
struct SBufferInfo
{
	SBufferInfo(){ sendc=0; ackc=0; lastsenttime=0; }
	size_t bid;
	size_t sendc;
	unsigned int lastsenttime;
	size_t ackc;
};

/**
* Data structure to save informations about a peer
**/
struct SPeer
{
	bool operator<(const SPeer &other)
	{
		return curr_state<other.curr_state;
	}
	unsigned int ip;
	unsigned short port;
	unsigned int id;
	std::map<unsigned int, SRtt> latencies;
	unsigned int c_wnd;
	int curr_state;
	std::vector<SQValues> qtable;
	int last_action;
	int last_cong_state;
	int sthresh;
	//std::map<size_t, SBufferInfo*> binfo;
	SOCKET s;
	unsigned int ack_packets;
	float server_rtt;
};

/**
* Data structure to save information about which states peers are in when the packet is send
**/
struct SQSubuserdata
{
	int state;
	int action;
};

/**
* Data structure to save information about individual peer states and the message size
**/
struct SQUserdata
{
	std::vector<SQSubuserdata> data;
	int msgsize;
};

/**
* Data structure to save information about a sent message
**/
struct SMessage
{
	SMessage(unsigned int pID, const std::vector<unsigned int> &pRoute, unsigned int pSenttime) : id(pID), route(pRoute), senttime(pSenttime) { acked=false;}
	bool operator<(const SMessage &other)
	{
		return timeouttime>other.timeouttime;
	}

	bool acked;
	unsigned int id;
	std::vector<unsigned int> route;
	unsigned int senttime;
	unsigned int timeouttime;
	SQUserdata qdata;
};

/**
* The Controller Thread
**/
class Controller
{
public:
	/**
	* Setup Controller giving the other threads so it can interact with them.
	* Set the bandwidth the controller should maximally use.
	**/
	Controller(Input *pInput, Tracker *pTracker, unsigned int bandwidth);

	/**
	* Add a new peer to the controller using IP, port, the peer socket and the initial bandwidth the peer published
	**/
	void addNewPeer(unsigned int ip, unsigned short port, SOCKET s, unsigned int bw);
	/**
	* Remove a peer spcified by its ip and port
	**/
	void removePeer(unsigned int ip, unsigned short port);

	/**
	* Get information about all peers
	**/
	std::vector<SBest> getBestNodes(void);

	/**
	* Main thread function
	**/
	void operator()(void);
private:
	
	//update the data structure about the best nodes
	void updateBestNodes(void);
	//construct a random sequence of length len and numbers smaller than len; bigger than zero and unique.
	//E.g. random_sequence(4) gives 1,2,0,3.
	std::vector<size_t> random_sequence(size_t len);
	//Returns if the buffer with it bid is spread to all clients by the current tree structure
	bool isSpread(size_t bid);
	//Reinforce a q-value in Peer pi with specific state and action with ret
	void reinforceQValue(SPeer *pi, int state, int action, double ret);
	//Update the congestion controll with an received ack
	void addAckPacket(SPeer *pi);
	//update the q-value: The state the congestion controller is in is updated.
	void updateQValue(SPeer *pi);

	//Returns the esimated latency from client with id 'from' to client with id 'to'
	float getLatency(unsigned int from, unsigned int to);

	//Handle the timeout of message msg
	void handleTimeout(SMessage* msg);
	//Handle an acknowledgement of message msg
	void handleAck(SMessage* msg);
	//update the RTT using message msg (for which an ACK was received) and the delay from last client to server
	void updateRtts(float server_rtt, SMessage *msg);
	//update the latency from 'from' to 'to' with newrtt
	void updateSingleLatency(unsigned int from, unsigned int to, float newrtt);

	//Data structures to save information about the peers(clients)
	std::map<unsigned int, SPeer> peers;
	//Connect ip and port with the peer(client) ids
	std::map<std::pair<unsigned int, unsigned short>, unsigned int> peers_ids;
	//Data structure to save information about Buffers
	std::map<size_t, std::vector<SBufferInfo*> > buffer_info;

	//Saved userdata of messages
	std::map<size_t, SQUserdata*> userdata;
	size_t npeers;
	//Next assignable peer(client) id
	unsigned int peer_id;

	//Bandwidth used for exploration
	unsigned int bandwidth_exploration;
	//Bandwidth used for exploitation
	unsigned int bandwidth_exploitation;

	//Pointers to other threads
	Input *input;
	Tracker *tracker;

	//Mutex to synchronize access to the best_* data structures
	boost::mutex mutex;
	//Information about peers for the tracker thread
	std::vector<SBest> best_nodes;
	std::vector<SBest> l_best_nodes;

	//List of messages of which timeout can still occurr
	std::list<SMessage*> msgs_timeouts;
	//Messages in garbage waiting to be deleted
	std::queue<SMessage*> msgs_garbage;
	//Mapps client and message id to Message
	std::map<unsigned int, std::map<unsigned int, SMessage*> > sent_msgs;

	//UDP server socket
	SOCKET csock;
};

#endif //CONTROLLER_H