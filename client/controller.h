/**
* Controller thread. Receives exploration/exploitation UDP messages from other 
* clients or from the server and forwards them to the next client in the chain
* or to its children in the tree.
* If this node is the last client in a exploration packet it sends an acknowledgement
* via the trackerconnector thread.
**/

#include "../common/types.h"
#include "../common/msg_spread.h"
#include "../common/msg_data.h"

class TrackerConnector;
class Output;

#include <boost/thread/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/bind.hpp>

/**
* Structure to save UDP messages that are sent asynchroniously
**/
struct SSendUDP
{
	char *buf;
	size_t bsize;
	unsigned int ip;
	unsigned short port;
};

/**
* Thread to send UDP messages and messages to the tracker asynchroniously
**/
class SendMessageThread
{
public:
	/**
	* Initialize the thread with the trackerconnector 'pTracker_conn' and outgoing udp socket 'udpsock'
	**/
	SendMessageThread(TrackerConnector *pTracker_conn, SOCKET udpsock);

	/**
	* Message queue thread
	**/
	void operator()(void);

	/**
	* Send data 'msg' to the tracker
	**/
	void sendToTracker(const CWData &msg);

	/**
	* Send data 'buf' of size 'bsize' to peer with ip 'ip' and port 'port using UDP
	**/
	void sendToUDP(const char *buf, size_t bsize, unsigned int ip, unsigned short port);

private:

	//Mutex and condition to lock and notifiy queue changes
	boost::mutex mutex;
	boost::condition cond;

	//Data that has to be send to the tracker
	std::vector<CWData> to_tracker;
	//Data that has to be send to a peer via udp
	std::vector<SSendUDP> to_udp;

	//Pointer to the trackerconnector
	TrackerConnector *tracker_conn;
	//UDP socket that is used to send the messages
	SOCKET cs;
};

/**
* Thread to receive, forward and acknowledge UDP packets
**/
class Controller
{
public:
	/**
	* Initialize the contorller. It should listen on UDP port 'pPort' and only utilize
	* bandwidth 'pBandwidth_out' (bytes/s).
	* With pointer to trackerconnector 'pTracker_conn' and output thread 'pOutput'
	**/
	Controller(unsigned short pPort, TrackerConnector *pTracker_conn, unsigned int pBandwidth_out, Output *pOutput);

	/**
	* main thread function
	**/
	void operator()(void);

	/**
	* Send data 'msg' to tracker
	**/
	void sendToTracker(const CWData &msg);
private:
	/**
	* Handle a message that is send through the tree structure
	**/
	void ProcessSpreadMsg(msg_spread &msg, CRData &data);
	/**
	* Handle exploration messag with multiple hops
	**/
	void ProcessDataMsg(msg_data &msg);

	//Pointers to trackerconnector and output thread
	TrackerConnector *tracker_conn;
	Output *output;

	//Thread to send messages asynchonously
	SendMessageThread *message_thread;

	//UDP listen socket
	SOCKET cs;

	//Port we listen on
	unsigned short port;

	//Maximal available bandwidth
	unsigned int bandwidth_max;
	//Current used bandwidth
	unsigned int bandwidth_curr;
	//Next time 'bandwidth_curr' should be set to zero (once per second)
	unsigned int last_bandwidth_reset;


	//Structure to save which packets it already forwarded to its children
	std::map<unsigned int, bool> packets_forward;
};