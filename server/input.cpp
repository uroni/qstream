#include "input.h"
#include "../common/stringtools.h"
#include "../common/socket_functions.h"
#include "../common/os_functions.h"
#include "../common/log.h"
#include <memory.h>

//Time a buffer should be kept in ms
const unsigned int buffer_time=5000;
//Size of a buffer
const unsigned int buffer_size=1440;

/**
* Initialize the input thread. Use the URL pURL for acessing the stream
* via HTTP
*/
Input::Input(const std::string &pURL) : url(pURL)
{
	curr_buffer_id=0;
	last_packetcounttime=os_gettimems();
	packets=0;
	packets_sec=0;
	max_packets_sec=0;
}

/**
* main thread function
**/
void Input::operator()(void)
{
	SOCKET server_socket=os_createSocket(false);

	//Tokenize the url - get server query and port
	std::string server=getbetween("http://","/", url);
	std::string server_name=server;
	std::string query=getafter("http://"+server_name, url);
	unsigned short server_port=80;
	if(server.find(":")!=std::string::npos)
	{
		server_name=getuntil(":", server);
		server_port=atoi(getafter(":", server).c_str());
	}

	//Get server ip, connect and send HTTP Request
	unsigned int server_ip=os_resolv(server_name);

	if(!os_connect(server_socket, server_ip, server_port) )
	{
		log("Could not connect to streaming input server. Aborting.");
		return;
	}

	std::string req="GET "+query+" HTTP/1.1\r\n"
					"Host: "+server+"\r\n"
					"User-Agent: QStream client\r\n"
					"Accept: */*\r\n"
					"Connection: close\r\n\r\n";

	os_send(server_socket, req.c_str(), req.size());

	//Receive the buffers
	int state=0;
	int rc;
	do
	{
		SBuffer *nb;
		{
			boost::mutex::scoped_lock lock(mutex);
			if(buffer_trash.empty() || (os_gettimems()-buffer_trash.front()->created)<500)
			{
				nb=new SBuffer;
				nb->data=new char[buffer_size];
			}
			else
			{
				nb=buffer_trash.front();
				buffer_trash.pop();
			}		
		}
		int offset=0;
		rc=os_recv(server_socket, nb->data, buffer_size);
		if(rc>0)
		{
			//If we're in state 0 the header hasn't been received yet. Look for it and switch to state 1
			if(state==0) //header
			{
				for(size_t i=0;i<(size_t)rc;++i)
				{
					if(i>=4 && nb->data[i]=='\n' && nb->data[i-1]=='\r' && nb->data[i-2]=='\n' && nb->data[i-3]=='\r' )
					{
						state=1;
						offset=i+1;
					}
				}
			}
			//Header has been received. Collect buffers
			if(state==1 && rc-offset>0 ) // body
			{
				if(offset!=0)
				{
					char *nd=new char[buffer_size];
					memcpy(nd, nb->data, rc);
					delete [] nb->data;
					nb->data=nd;
				}				
				//Create new buffers

				nb->datasize=rc-offset;
				nb->created=os_gettimems();
				nb->id=++curr_buffer_id;			

				boost::mutex::scoped_lock lock(mutex);
				if(os_gettimems()- last_packetcounttime>1000 && packets!=0)
				{
					packets_sec=0.9f*packets_sec+0.1f*(float)packets;
					if(packets_sec>max_packets_sec)
					{
						max_packets_sec=packets_sec;
					}
					packets=0;
					last_packetcounttime=os_gettimems();
				}
				++packets;
				new_buffers.push(nb);
				buffer_ids.insert(std::pair<size_t, SBuffer*>(nb->id, nb) );
			}
		}

		//Remove old buffers
		cleanBuffer();
	}
	while(rc>0);
}

/**
* Clean old buffers
**/
void Input::cleanBuffer(void)
{
	boost::mutex::scoped_lock lock(mutex);
	//Remove old buffers and trash them
	do
	{
		if(!old_buffers.empty())
		{
			SBuffer *nm=old_buffers.front();
			//if the buffer is older than buffer_time trash it else stop cleaning
			if(os_gettimems()-nm->created>buffer_time)
			{
				old_buffers.pop();
				std::map<size_t, SBuffer*>::iterator it=buffer_ids.find(nm->id);
				if(it!=buffer_ids.end())
				{
					buffer_ids.erase(it);
				}
				nm->created=os_gettimems();
				buffer_trash.push(nm);
			}
			else
			{
				break;
			}
		}
		else
		{
			break;
		}
	}
	while(true);
}

/**
* Get the average amount of packets received per second
**/
float Input::getPacketsPerSecond(void)
{
	boost::mutex::scoped_lock lock(mutex);
	return packets_sec;
}

/**
* Get the maximal amount of packets received per second
**/
float Input::getMaxPacketsPerSecond(void)
{
	boost::mutex::scoped_lock lock(mutex);
	return max_packets_sec;
}

/**
* Get the new buffers
**/
std::vector<SBuffer*> Input::getNewBuffers(void)
{
	boost::mutex::scoped_lock lock(mutex);
	std::vector<SBuffer*> nb;
	while(!new_buffers.empty())
	{
		new_buffers.front()->already_used=false;
		nb.push_back(new_buffers.front());
		old_buffers.push(new_buffers.front());
		new_buffers.pop();
	}
	return nb;
}

/**
* Get a specific buffer
**/
SBuffer* Input::getBuffer(size_t id)
{
	boost::mutex::scoped_lock lock(mutex);
	std::map<size_t, SBuffer*>::iterator it=buffer_ids.find(id);
	if(it!=buffer_ids.end())
	{
		return it->second;
	}
	else
	{
		return NULL;
	}
}