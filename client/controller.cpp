#ifdef _WIN32
#include <windows.h>
#endif
#include "../common/socket_functions.h"
#include "../common/os_functions.h"
#include "../common/log.h"
#include "../common/stringtools.h"
#include "../common/packet_ids.h"
#include "../common/msg_ack.h"
#include "trackerconnector.h"
#include "output.h"
#include "controller.h"
#include <memory.h>

/**
* Initialize the contorller. It should listen on UDP port 'pPort' and only utilize
* bandwidth 'pBandwidth_out' (bytes/s).
* With pointer to trackerconnector 'pTracker_conn' and output thread 'pOutput'
**/
Controller::Controller(unsigned short pPort, TrackerConnector *pTracker_conn, unsigned int pBandwidth_out, Output *pOutput) :
	tracker_conn(pTracker_conn), port(pPort), output(pOutput)
{
	bandwidth_max=pBandwidth_out;
	bandwidth_curr=0;
	last_bandwidth_reset=os_gettimems();
}

/**
* main thread function
**/
void Controller::operator()(void)
{
#ifdef _WIN32
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

	cs=os_createSocket();
	if(!os_bind(cs, port) )
	{
		log("Error binding socket to port "+nconvert(port));
		return;
	}

	unsigned int send_window_size=1024*1024; //1MB
	while(!os_set_send_window(cs, send_window_size) )
		send_window_size/=2;

	log("Send buffer set to "+nconvert(send_window_size));

	unsigned int recv_window_size=1024*1024; //1MB
	while(!os_set_recv_window(cs, recv_window_size) )
		recv_window_size/=2;

	log("Receive buffer set to "+nconvert(recv_window_size));

	message_thread=new SendMessageThread(tracker_conn, cs);
	boost::thread message_thread_d(boost::ref(*message_thread));
	message_thread_d.yield();

	while(true)
	{
		char buffer[4096];
		unsigned int sourceip;
		unsigned short sourceport;
		int rc=os_recvfrom(cs, buffer, 4096, sourceip, sourceport);
		if(os_gettimems()-last_bandwidth_reset>1000)
		{
			bandwidth_curr=0;
			last_bandwidth_reset=os_gettimems();
		}
		if(rc>0)
		{
			CRData data(buffer, rc);
			unsigned char id;
			data.getUChar(&id);
			switch(id)
			{
			case CC_SPREAD:
				{
					msg_spread msg(data);
					ProcessSpreadMsg(msg, data);
				}break;
			case CC_DATA:
				{
					msg_data msg(data);
					ProcessDataMsg(msg);
				}break;
			}
		}
	}
}

/**
* Handle a message that is send through the tree structure
**/
void Controller::ProcessSpreadMsg(msg_spread &msg, CRData &data)
{
	std::map<unsigned int, bool>::iterator it=packets_forward.find(msg.getMsgID());
	if(it==packets_forward.end())
	{
		packets_forward[msg.getMsgID()]=true;
		while(packets_forward.size()>1000)
			packets_forward.erase(packets_forward.begin());

		{
			char *buf=new char[msg.getBuf_size()];
			memcpy(buf, msg.getBuf(), msg.getBuf_size());
			SBufferObject *obj=new SBufferObject;
			obj->id=msg.getMsgID();
			obj->buf=buf;
			obj->bsize=msg.getBuf_size();
			output->addBufferObject(obj);
		}

		std::vector<std::pair<unsigned int, unsigned short> > peers=tracker_conn->getPeers(msg.getMsgID());
		for(size_t i=0;i<peers.size();++i)
		{
			//if(bandwidth_curr<bandwidth_max)
			{
				message_thread->sendToUDP(data.getDataPtr(), data.getSize(), peers[i].first, peers[i].second);
				bandwidth_curr+=data.getSize();
			}
			/*else
			{
				LOG("Not enough spread bandwidth!", LL_INFO);
				break;
			}*/
		}
	}
}

/**
* Handle exploration messag with multiple hops
**/
void Controller::ProcessDataMsg(msg_data &msg)
{
	{
		char *buf=new char[msg.getBuf_size()];
		memcpy(buf, msg.getBuf(), msg.getBuf_size());
		SBufferObject *obj=new SBufferObject;
		obj->id=msg.getMsgID();
		obj->buf=buf;
		obj->bsize=msg.getBuf_size();
		output->addBufferObject(obj);
	}

	std::pair<unsigned int, unsigned short> next=msg.getNextHop();
	if(next.first!=0)
	{
		if(bandwidth_curr<bandwidth_max)
		{
			msg.incrementHop();
			CWData data;
			msg.getMessage(data);
			message_thread->sendToUDP(data.getDataPtr(), data.getDataSize(), next.first, next.second);
			bandwidth_curr+=data.getDataSize();
		}
	}
	else
	{
		next=msg.getTarget();
		if(next.first!=0)
		{
			msg_ack ackmsg(msg.getMsgID(), next.first, next.second);
			CWData data;
			ackmsg.getMessage(data);
			message_thread->sendToTracker(data);
			LOG("ACK for ID="+nconvert(msg.getMsgID()), LL_DEBUG );
		}
	}
}

/**
* Initialize the thread with the trackerconnector 'pTracker_conn' and outgoing udp socket 'udpsock'
**/
SendMessageThread::SendMessageThread(TrackerConnector *pTracker_conn, SOCKET udpsock) : tracker_conn(pTracker_conn), cs(udpsock)
{
}

/**
* Message queue thread
**/
void SendMessageThread::operator()(void)
{
	//SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	boost::mutex::scoped_lock lock(mutex);
	while(true)
	{
		cond.wait(lock);
		for(size_t i=0;i<to_tracker.size();++i)
		{
			CWData data=to_tracker[i];
			lock.unlock();
			tracker_conn->sendToTracker( data );
			lock.lock();
		}
		to_tracker.clear();
		for(size_t i=0;i<to_udp.size();++i)
		{
			os_sendto(cs, to_udp[i].ip, to_udp[i].port, to_udp[i].buf, to_udp[i].bsize );
			delete [] to_udp[i].buf;
		}
		to_udp.clear();
	}
}

/**
* Send data 'msg' to the tracker
**/
void SendMessageThread::sendToTracker(const CWData &msg)
{
	boost::mutex::scoped_lock lock(mutex);
	to_tracker.push_back(msg);
	cond.notify_all();
}

/**
* Send data 'buf' of size 'bsize' to peer with ip 'ip' and port 'port using UDP
**/
void SendMessageThread::sendToUDP(const char *buf, size_t bsize, unsigned int ip, unsigned short port)
{
	SSendUDP ns;
	ns.buf=new char[bsize];
	memcpy(ns.buf, buf, bsize);
	ns.bsize=bsize;
	ns.ip=ip;
	ns.port=port;
	boost::mutex::scoped_lock lock(mutex);
	to_udp.push_back(ns);
	cond.notify_all();
}

/**
* Send data 'msg' to tracker
**/
void Controller::sendToTracker(const CWData &msg)
{
	message_thread->sendToTracker(msg);
}