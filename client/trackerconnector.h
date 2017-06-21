/**
* Thread that connects itself to the tracker. Announces the port this clients listens for UDP packets and the
* bandwidth it thinks it is able to handle. Receives the children of this node fore different stream slices
* and responds to pings.
**/
#include "../common/types.h"
#include "../common/tcpstack.h"
#include "../common/data.h"
#include <boost/thread/mutex.hpp>

class TrackerConnector
{
public:
	/**
	* Initialize the tracker connector by giving the name of the tracker (ip or dns-name) 'pTracker' the port on which the tracker
	* accepts tcp connections, the port which is used by this client to receive udp packets and the bandwidth this client has to
	* forward packets.
	**/
	TrackerConnector(std::string pTracker, unsigned short pTrackerport, unsigned short pControllerport, unsigned int pBandwidth_out);

	/**
	* Main thread function
	**/
	void operator()(void);

	/**
	* Returns a list of ip,port pairs of children for a message with id 'msgid'. The msgid is used to
	* calculate which slice the message is in.
	**/
	std::vector<std::pair<unsigned int, unsigned short> > getPeers(unsigned int msgid);

	/**
	* Send data 'data' to the tracker using the TCP connection
	**/
	void sendToTracker(CWData &data);

private:
	/**
	* Handle the message 'msg' received from the tracker
	**/
	void receivePacket(CRData &msg);

	//name of the tracker
	std::string tracker;
	//tcp port of the tracker
	unsigned short trackerport;
	//port this clients listens for UDP packets
	unsigned short controllerport;
	//List of children this node has for each of the k trees. The List of children consists of ip, port pairs
	std::vector<std::vector<std::pair<unsigned int, unsigned short> > > peers;
	//Mutex to synchonize accesses to the class that handles tcp packeting
	boost::mutex mutex;
	//class to packetize tcp messages
	CTCPStack stack;
	//tcp socket
	SOCKET cs;
	//Available bandwidth
	unsigned int bandwidth_out;
};