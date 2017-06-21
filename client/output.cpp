#ifdef _WIN32
#include <windows.h>
#endif
#include "output.h"
#include "controller.h"
#include "../common/socket_functions.h"
#include "../common/os_functions.h"
#include "../common/log.h"
#include "../common/stringtools.h"
#include "../common/data.h"
#include <string.h>
#include "../common/packet_ids.h"

/**
* Initialize output thread. Listen on port 'pPort'
**/
Output::Output(unsigned short pPort) : port(pPort)
{
	next_id=0;
	past_id=0;
	ints=false;
	tsleft=-1;
	nack_next=false;
	time_last=0;
}

/**
* Main thread function
**/
void Output::operator()(void)
{
	SOCKET cs=os_createSocket(false);
	if(!os_bind(cs, port))
	{
		log("error binding socket to port "+nconvert(port));
		return;
	}

	os_listen(cs,1000);

	clients.push_back(cs);

	while(true)
	{
		std::vector<SOCKET> md=os_select(clients, 1000);
		for(size_t i=0;i<md.size();++i)
		{
			if(md[i]==cs)
			{
				log("New Output client");
				SOCKET ns=os_accept(cs);
				clients.push_back(ns);
				const char *vv="HTTP/1.0 200 OK\r\nContent-Type: video/mpeg\r\n\r\n";
				os_send(ns, vv, strlen(vv));
			}
			else
			{
				char buffer[4096];
				int rc=os_recv(md[i],buffer,4096);
				if(rc<=0)
				{
					log("Lost connection to Output client");
					for(size_t j=0;j<clients.size();++j)
					{
						if(clients[j]==md[i])
						{
							clients.erase(clients.begin()+j);
							break;
						}
					}
				}
			}
		}

		{
			if(os_gettimems()-time_last>2000)
			{
				if(nack_next==false)
				{
					nack_next=true;
					CWData data;
					data.addUChar(TRACKER_NACK);
					data.addUInt(next_id);
					controller->sendToTracker(data);
				}
			}

			boost::mutex::scoped_lock lock(mutex);
			while(!buffers.empty() && (buffers.begin()->second->id==next_id || next_id==0
				|| (os_gettimems()-buffers.begin()->second->atime)>3000) )
			{
				time_last=os_gettimems();
				nack_next=false;
				if(buffers.begin()->second->id!=next_id)
				{
					while(!tspackets.empty())
					{
						for(size_t i=0;i<tspackets.front().size();++i)
						{
							--tspackets.front()[i].ref->refc;
							if(tspackets.front()[i].ref->refc==0)
							{
								delete [] tspackets.front()[i].ref->buf;
								delete tspackets.front()[i].ref;
							}
						}
						tspackets.pop();
					}
					if(stream_ok)
					{
						stream_ok=false;
						log("Stream not okay. Skipped "+nconvert(buffers.begin()->second->id-next_id)+" packets");
					}
					ints=false;
				}
				else
				{
					if(!stream_ok)
					{
						stream_ok=true;
						log("Stream is okay");
					}
				}
				SBufferObject *obj=buffers.begin()->second;
				past_id=obj->id;
				next_id=obj->id+1;
				obj->refc=1;
				lock.unlock();
				size_t offset=0;
				if(ints==false)
				{
					for(size_t i=0;i<obj->bsize;++i)
					{
						if(obj->buf[i]==0x47 && i+188<obj->bsize && obj->buf[i+188]==0x47 )
						{
							TSPacket tsp;
							tsp.buf=&obj->buf[i];
							tsp.bsize=188;
							tsp.ref=obj;
							++obj->refc;
							std::vector<TSPacket> tp;
							tp.push_back(tsp);
							tspackets.push(tp);
							offset=i+188;
							ints=true;
							break;
						}
					}
				}
				if(ints==true && !tspackets.empty() && getSize(tspackets.back())<188)
				{
					size_t left=188-getSize(tspackets.back());
					if(left<obj->bsize && obj->buf[left]==0x47)
					{
						TSPacket tsp;
						tsp.buf=obj->buf;
						tsp.bsize=left;
						tsp.ref=obj;
						tspackets.back().push_back(tsp);
						++obj->refc;
						offset=left;
					}
					else if(left>=obj->bsize)
					{
						TSPacket tsp;
						tsp.buf=obj->buf;
						tsp.bsize=obj->bsize;
						tsp.ref=obj;
						tspackets.back().push_back(tsp);
						++obj->refc;
						offset=left;
					}
					else
					{
						log("OUTPUT: left packet not found");
						for(size_t i=0;i<tspackets.back().size();++i)
						{
							--tspackets.back()[i].ref->refc;
							if(tspackets.back()[i].ref->refc==0)
							{
								delete [] tspackets.back()[i].ref->buf;
								delete tspackets.back()[i].ref;
							}
						}
						tspackets.back().clear();
						ints=false;
					}
				}
				if(ints==true && offset<obj->bsize)
				{
					while(true)
					{
						if(offset<obj->bsize && obj->buf[offset]==0x47)
						{
							if(offset+188<obj->bsize && obj->buf[offset+188] )
							{
								TSPacket tsp;
								tsp.buf=&obj->buf[offset];
								tsp.bsize=188;
								tsp.ref=obj;
								++obj->refc;
								std::vector<TSPacket> tp; tp.push_back(tsp);
								tspackets.push(tp);
								offset+=188;
							}
							else
							{
								TSPacket tsp;
								tsp.buf=&obj->buf[offset];
								tsp.bsize=obj->bsize-offset;
								tsp.ref=obj;
								++obj->refc;
								std::vector<TSPacket> tp; tp.push_back(tsp);
								tspackets.push(tp);
								break;
							}
						}
						else
						{
							log("Offset wrong");
							ints=false;
							break;
						}
					}
				}

				while(!tspackets.empty())
				{
					if(tspackets.front().empty())
					{
						tspackets.pop();
						continue;
					}

					if(getSize(tspackets.front())==188)
					{
						for(size_t i=1;i<clients.size();++i)
						{
							for(size_t k=0;k<tspackets.front().size();++k)
							{
								os_send(clients[i], tspackets.front()[k].buf, tspackets.front()[k].bsize);
							}
						}
						for(size_t k=0;k<tspackets.front().size();++k)
						{
							--tspackets.front()[k].ref->refc;
							if(tspackets.front()[k].ref->refc==0)
							{
								delete []tspackets.front()[k].ref->buf;
								delete tspackets.front()[k].ref;
							}
						}
						tspackets.pop();
					}
					else
					{
						break;
					}
				}

				
				--obj->refc;
				if(obj->refc==0)
				{
					delete [] obj->buf;
					delete obj;
				}

				lock.lock();
				buffers.erase(buffers.begin());
			}
		}
	}
}

/**
* Add a buffer that should be send to all connected video players
**/
void Output::addBufferObject(SBufferObject *obj)
{
	boost::mutex::scoped_lock lock(mutex);
	obj->atime=os_gettimems();
	if(obj->id>past_id || past_id==0)
	{
		std::map<unsigned int,SBufferObject*>::iterator iter=buffers.find(obj->id);
		if(iter==buffers.end())
		{
			obj->atime=os_gettimems();
			obj->nack=false;
			buffers[obj->id]=obj;
		}
		else
		{
			delete [] obj->buf;
			delete obj;
		}
	}
}

/**
* Add a buffer that should be send to all connected video players
**/
size_t Output::getSize(const std::vector<TSPacket> &tsp)
{
	size_t ret=0;
	for(size_t i=0;i<tsp.size();++i)
	{
		ret+=tsp[i].bsize;
	}
	return ret;
}

/**
* Set the pointer to the controller thread
**/
void Output::setController(Controller *pController)
{
	controller=pController;
}