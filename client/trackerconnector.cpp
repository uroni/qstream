#ifdef _WIN32
#include <windows.h>
#endif
#include "trackerconnector.h"
#include "../common/socket_functions.h"
#include "../common/log.h"
#include "../common/stringtools.h"
#include "../common/data.h"
#include "../common/packet_ids.h"
#include "../common/msg_tree.h"

/**
* Initialize the tracker connector by giving the name of the tracker (ip or dns-name) 'pTracker' the port on which the tracker
* accepts tcp connections, the port which is used by this client to receive udp packets and the bandwidth this client has to
* forward packets.
**/
TrackerConnector::TrackerConnector(std::string pTracker, unsigned short pTrackerport, unsigned short pControllerport, unsigned int pBandwidth_out)
: tracker(pTracker), trackerport(pTrackerport), controllerport(pControllerport), bandwidth_out(pBandwidth_out)
{

}

/**
* Main thread function
**/
void TrackerConnector::operator()(void)
{
	unsigned int trackerip=os_resolv(tracker);
	cs=os_createSocket(false);
	bool b=os_connect(cs, trackerip, trackerport);
	if(!b)
	{
		log("Could not connect to tracker "+tracker+" on port "+nconvert(trackerport));
		os_closesocket(cs);
		return;
	}

	log("Connected to tracker.");
	os_nagle(cs, false);

	{
		CWData msg;
		msg.addUChar(TRACKER_PORT);
		msg.addUShort(controllerport);
		msg.addUInt(bandwidth_out);
		stack.Send(cs,msg);
	}

	while(true)
	{
		char buffer[4096];
		int rc=os_recv(cs, buffer, 4096);
		if(rc<=0)
		{
			log("Connection to tracker lost!");
			os_closesocket(cs);
			return;
		}
		else
		{
			stack.AddData(buffer, rc);
			size_t bsize;
			char *buf;
			while( (buf=stack.getPacket(&bsize))!=NULL)
			{
				CRData msg(buf, bsize);
				receivePacket(msg);
				delete []buf;
			}
		}
	}
}

/**
* Returns a list of ip,port pairs of children for a message with id 'msgid'. The msgid is used to
* calculate which slice the message is in.
**/
std::vector<std::pair<unsigned int, unsigned short> > TrackerConnector::getPeers(unsigned int msgid)
{
	boost::mutex::scoped_lock lock(mutex);
	if(peers.empty())
		return std::vector<std::pair<unsigned int, unsigned short> >();

	int k=msgid%peers.size();
	if(k<(int)peers.size())
	{
		return peers[k];
	}
	else
	{
		return std::vector<std::pair<unsigned int, unsigned short> >();
	}
}

/**
* Handle the message 'msg' received from the tracker
**/
void TrackerConnector::receivePacket(CRData &msg)
{
	unsigned char type;
	if(msg.getUChar(&type) )
	{
		switch(type)
		{
		case TRACKER_PING:
			{
				CWData repl;
				repl.addUChar(TRACKER_PONG);
				stack.Send(cs, repl);
			}break;
		case TRACKER_TREE:
			{
				msg_tree tree(msg);
				if(!tree.hasError())
				{
					boost::mutex::scoped_lock lock(mutex);
					if(peers.size()!=tree.getSlices())
					{
						peers.resize(tree.getSlices());
					}
					peers[tree.getK()]=tree.getRelayNodes();
				}
				else
				{
					log("tree message has error");
				}
			};
		}
	}
}

/**
* Send data 'data' to the tracker using the TCP connection
**/
void TrackerConnector::sendToTracker(CWData &data)
{
	stack.Send(cs, data);
}