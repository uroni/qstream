#ifdef _WIN32
#include <windows.h>
#endif
#include "controller.h"
#include "../common/log.h"
#include "../common/os_functions.h"
#include "../common/msg_data.h"
#include "../common/msg_spread.h"
#include "../common/stringtools.h"
#include <algorithm>

//Desired redundancy of the messages
const size_t msg_redundancy=1;
//Target size of one packets (usually mtu)
const unsigned int msg_packetsize=1500;
//Number of desired hops of exploration packets
const size_t c_hops=3;
//Delay between timesteps. Server will send (1000/bandwidth_timestep) packets per second
const unsigned int bandwidth_timestep=100;
//Reinforcement learning parameters
const float reinf_up=0.1f;
const float reinf_down=-1.0f;
const float alpha=0.1f;
const float lambda=1.0f;
const float epsilon=0.1f;
//Maximal size of the q-table
const size_t qtablesize=8738;
//Inital latency between peers in seconds
const float default_latency=0.5f;
//RTT estimation parameter
const float rttalpha=0.15f;

/**
* Setup Controller giving the other threads so it can interact with them.
* Set the bandwidth the controller should maximally use.
**/
Controller::Controller(Input *pInput, Tracker *pTracker, unsigned int bandwidth) : input(pInput), tracker(pTracker)
{
	npeers=0;
	peer_id=1;
	bandwidth_exploration=(unsigned int)((float)bandwidth*((float)bandwidth_timestep/1000.f)*0.1f+0.5f);
	bandwidth_exploitation=(unsigned int)((float)bandwidth*((float)bandwidth_timestep/1000.f)*0.9f+0.5f);
}

/**
* Add a new peer to the controller using IP, port, the peer socket and the initial bandwidth the peer published
**/
void Controller::addNewPeer(unsigned int ip, unsigned short port, SOCKET s, unsigned int bw)
{
	++npeers;

	SPeer np;
	np.ip=ip;
	np.port=port;
	np.c_wnd=0;
	np.curr_state=0;
	np.id=peer_id;
	np.last_action=0;
	np.last_cong_state=-1;
	np.sthresh=bw/msg_packetsize;
	np.s=s;
	np.qtable.resize(qtablesize);
	np.server_rtt=1;

	peers_ids.insert(std::pair<std::pair<unsigned int, unsigned short>, unsigned int>(std::pair<unsigned int, unsigned short>(ip, port), peer_id) );
	peers.insert(std::pair<unsigned int, SPeer>(peer_id, np) );

	++peer_id;
}

/**
* Remove a peer spcified by its ip and port
**/
void Controller::removePeer(unsigned int ip, unsigned short port)
{
	//find peer id
	std::map<std::pair<unsigned int, unsigned short>, unsigned int>::iterator iter=peers_ids.find(std::pair<unsigned int, unsigned short>(ip, port));
	if(iter!=peers_ids.end())
	{
		//remove peer info
		std::map<unsigned int, SPeer>::iterator it=peers.find(iter->second);
		if(it!=peers.end())
		{
			peers.erase(it);
		}
		//remove data
		for(size_t k=0;k<best_nodes.size();++k)
		{
			if(best_nodes[k].id==iter->second)
			{
				best_nodes.erase(best_nodes.begin()+k);
				break;
			}
		}
		peers_ids.erase(iter);
	}
}

/**
* construct a random sequence of length len and numbers smaller than len; bigger than zero and unique.
* E.g. random_sequence(4) gives 1,2,0,3.
**/
std::vector<size_t> Controller::random_sequence(size_t len)
{
	std::vector<size_t> ret;
	//add all members
	ret.resize(len);
	for(size_t i=0;i<len;++i)
	{
		ret[i]=i;
	}

	//shuffle it a bit
	for(size_t i=0;i<len;++i)
	{
		size_t f1=rand()%len;
		size_t f2=rand()%len;
		size_t t=ret[f1];
		ret[f1]=ret[f2];
		ret[f2]=t;
	}

	return ret;
}

/**
* Main thread function
**/
void Controller::operator()(void)
{
	csock=os_createSocket();
	if(!os_bind(csock, 5700))
	{
		log("error binding UDP socket to port 5700");
		return;
	}

	//increase the udp send buffer (operating system)
	unsigned int send_window_size=1024*1024; //1MB
	while(!os_set_send_window(csock, send_window_size) )
		send_window_size/=2;

	log("Send buffer set to "+nconvert(send_window_size));

	unsigned int b_explore=0;
	unsigned int b_exploit=0;
	//next times the rates will be reset
	unsigned int b_next_reset=os_gettimems()+bandwidth_timestep;

	//new buffers from input thread
	std::vector<SBuffer*> new_bufs;
	//currently unused
	std::queue<SResend> new_resends;

	while(true)
	{
		//add new clients and remove clients that aren't connected anymore
		{
			std::vector<SNewClient> nc=tracker->getNewClients();
			for(size_t i=0;i<nc.size();++i)
			{
				addNewPeer(nc[i].ip, nc[i].port, nc[i].s, nc[i].bandwidth);
			}
			nc=tracker->getExitClients();
			for(size_t i=0;i<nc.size();++i)
			{
				removePeer(nc[i].ip, nc[i].port);
			}
		}

		//get new buffers from input thread and save them
		std::vector<SBuffer*> nb=input->getNewBuffers();
		new_bufs.insert(new_bufs.end(), nb.begin(), nb.end() );

		std::vector<SResend> nr=tracker->getNewResends();
		//for(size_t i=0;i<nr.size();++i)
		//	new_resends.push(nr[i]); //disabled

		//measure performance
		unsigned int exploit_time=os_gettimems();

		if(!new_bufs.empty())
		{		
			size_t delbufs=0;
			bool ex_one=false;

			//exploit
			{
				//resending (currently unused)
				while(!new_resends.empty())
				{
					SResend &r=new_resends.front();
					SBuffer *buf=input->getBuffer(r.msgid);
					if(buf==NULL)
					{
						new_resends.pop();
					}
					else
					{
						std::map<std::pair<unsigned int, unsigned short>, unsigned int>::iterator it=peers_ids.find(std::pair<unsigned int, unsigned short>(r.ip, r.port) );
						if(it!=peers_ids.end())
						{
							std::map<unsigned int, SPeer>::iterator peerit=peers.find(it->second);
							if(peerit!=peers.end())
							{
								if((int)peerit->second.c_wnd<peerit->second.curr_state+1)
								{
									if(b_exploit<bandwidth_exploitation)
									{
										std::vector<std::pair<unsigned int, unsigned short> > hops;
										hops.push_back(std::pair<unsigned int, unsigned short>(r.ip, r.port) );
										msg_data msg(r.msgid, hops, buf->data, buf->datasize);
										os_sendto(csock, r.ip, r.port, msg.getBuf(), msg.getBuf_size() );
										b_exploit+=msg.getBuf_size();
										++peerit->second.c_wnd;
									}
								}
							}
						}
					}
				}


				for(size_t i=0;i<new_bufs.size();++i)
				{
					//If sending takes so long that we take more then one cputimeslice -> Skip sleeping
					if(os_gettimems()>b_next_reset)
					{
						b_explore=0;
						b_exploit=0;
						b_next_reset=os_gettimems()+bandwidth_timestep;
					}

					//Remove buffers older than one second
					if(ex_one==false && os_gettimems()-new_bufs[i]->created>1000)
					{
						static unsigned int last_log_time=0;
						if(os_gettimems()-last_log_time>1000)
						{
							log("Buffer too old. Couldn't send it...");
							last_log_time=os_gettimems();
						}
						++delbufs;
						continue;
					}
					ex_one=true;
					//k is the slice number
					int k=new_bufs[i]->id%k_slices;
					std::map<unsigned int, unsigned int> sendc;
					size_t bid=new_bufs[i]->id;
					std::vector<SSpread> spread_nodes;
					unsigned b_old_exploit=b_exploit;
					bool is_spread=false;

					//Abort if the server hasn't any clients
					if(best_nodes.empty())
					{
						is_spread=true;
						++delbufs;
						continue;
					}

					//Get the tree for the slice
					spread_nodes=tracker->getSpreadNodes(k);
					//look if there's enough bandwidth available on the server side
					for(size_t k=0;k<spread_nodes.size();++k)
					{
						//Used for "not spread" warning message
						++sendc[spread_nodes[k].id];
						//If child is true the client is a direct child of the server and we have to send
						//the message to it
						if(spread_nodes[k].child)
						{
							b_exploit+=new_bufs[i]->datasize+7;
						}
					}
					//Look if the message is spread
					bool spread=true;
					for(std::map<unsigned int, SPeer>::iterator it=peers.begin();it!=peers.end();++it)
					{
						//If one client doesn't get the message it is not spread to every client
						if(sendc[it->first]<1)
						{
							spread=false;
						}
					}
					//Abort if sending this packet would exceed the server exploitation bandwidth (send it in the next timeslice)
					if(b_exploit>=bandwidth_exploitation)
						break;

					//reset the rates
					b_exploit=b_old_exploit;
					sendc.clear();
					{
						//Look if every client which is an interior node in the tree
						//has enough bandwidth available for forwarding this message
						bool load_ok=true;
						for(size_t k=0;k<spread_nodes.size();++k)
						{
							++sendc[spread_nodes[k].id];
							std::map<unsigned int, SPeer>::iterator it=peers.find(spread_nodes[k].id);
							if(it!=peers.end())
							{								
								if(it->second.c_wnd+spread_nodes[k].load>(unsigned int)it->second.curr_state)
								{
									load_ok=false;
									break;
								}
							}
						}
						//If the load is not okay display error message and don't send it
						if(load_ok)
						{
							for(size_t k=0;k<spread_nodes.size();++k)
							{
								//The node with id 0 is the root(the server)
								if(spread_nodes[k].id==0) continue;

								//Get peer data(for ip and port) and send the message if it is a direct child of the server
								std::map<unsigned int, SPeer>::iterator it=peers.find(spread_nodes[k].id);
								if(it!=peers.end())
								{
									if(spread_nodes[k].child)
									{
										msg_spread msg(new_bufs[i]->id, new_bufs[i]->data, new_bufs[i]->datasize);
										CWData data;
										msg.getMessage(data);
										os_sendto(csock, it->second.ip, it->second.port, data.getDataPtr(), data.getDataSize());
										//add the message size
										b_exploit+=data.getDataSize();
										//This shouldn't happen
										if(b_exploit>=bandwidth_exploitation)
											break;
									}
									//Add the sent data to the bandwidth
									if(spread_nodes[k].forward)
									{
										it->second.c_wnd+=spread_nodes[k].load;
									}					
								}
								else
								{
									log("peer not found");
								}
							}
						}
						else
						{
							static unsigned int last_load_msg=0;
							if(os_gettimems()-last_load_msg>1000)
							{
								log("Proposed load not okay");
								last_load_msg=os_gettimems();
							}
						}

						//Check again if the spread is okay
						for(std::map<unsigned int, SPeer>::iterator it=peers.begin();it!=peers.end();++it)
						{
							if(sendc[it->first]<1)
							{
								static unsigned int last_spread_msg=0;
								if(os_gettimems()-last_spread_msg>1000)
								{
									log("spread not okay");
									last_spread_msg=os_gettimems();
								}
							}
						}
						//Delete the buffer because it was sent to the tree
						++delbufs;
						//Stop if the bandwidth is exceeded
						if(b_exploit>=bandwidth_exploitation)
							break;
					}
					//Error message if the packet is not spread
					if(!spread)
					{
						static unsigned int last_spread_msg=0;
						if(os_gettimems()-last_spread_msg>1000)
						{
							log("spacket not spread");
							last_spread_msg=os_gettimems();
						}
					}
				}				
			}

			LOG("Exploittime: "+nconvert(os_gettimems()-exploit_time), LL_DEBUG);
			unsigned int explore_time=os_gettimems();

			//Collect all peer ids
			std::vector<unsigned int> peers_seq;
			for(std::map<unsigned int, SPeer>::iterator it=peers.begin();it!=peers.end();++it)
			{
				peers_seq.push_back(it->first);
			}
			for(size_t i=0;i<new_bufs.size();++i)
			{
				//If already used is true we already did exploration with this packet
				if(new_bufs[i]->already_used)
					continue;

				new_bufs[i]->already_used=true;

				//Get a random sequence
				std::vector<size_t> rnd_seq=random_sequence(peers_seq.size());
				SBuffer *buf=new_bufs[i];
							
				std::vector<std::pair<unsigned int, unsigned short> > msgpeers;
				std::vector<unsigned int> route;
				std::vector<SPeer*> route_peers;
				float latency=default_latency;
				SQUserdata qdata;
				qdata.msgsize=buf->datasize;
				for(size_t i=0;i<rnd_seq.size();++i)
				{
					//Get a random peer
					SPeer &cpeer=peers[peers_seq[rnd_seq[i]] ];
					if(cpeer.c_wnd>=(unsigned int)cpeer.curr_state+1)
					{
						continue;
					}
					else
					{
						//Add the peer and set the userdata
						msgpeers.push_back(std::pair<unsigned int, unsigned short>(cpeer.ip, cpeer.port) );
						route.push_back(cpeer.id);
						route_peers.push_back(&peers[peers_seq[rnd_seq[i]] ]);
						{
							SQSubuserdata sqd;
							sqd.action=cpeer.last_action;
							sqd.state=cpeer.curr_state;
							qdata.data.push_back(sqd);
						}
						{
							unsigned int source=0;
							if(route.size()>1)
								source=route[route.size()-2];
							latency+=getLatency(source, cpeer.id);
						}
						//If we have collected enough clients or if we are at the end of the random sequence send the message
						if(msgpeers.size()>c_hops || (i+1>=rnd_seq.size() && !msgpeers.empty() ) )
						{
							msg_data msg(buf->id, msgpeers, buf->data, buf->datasize);
							msg.incrementHop();
							CWData data;
							msg.getMessage(data);
							os_sendto(csock, msgpeers[0].first, msgpeers[0].second, data.getDataPtr(), data.getDataSize() );
							//Save the message for timeout checking
							{
								SMessage *sm=new SMessage(buf->id, route, os_gettimems());
								sm->timeouttime=sm->senttime+(unsigned int)(latency*1000.f+0.5f);
								sm->qdata=qdata;
								sent_msgs[buf->id][cpeer.id]=sm;
								msgs_timeouts.push_back(sm);
							}
							//Increase the sent messages for every client on the route
							if(route_peers.size()>1)
							{
								for(size_t k=0;k<route_peers.size()-1;++k)
								{
									++route_peers[k]->c_wnd;
								}
							}
							else
							{
								++route_peers[0]->c_wnd;
							}
							//Add exploration bandwidth
							b_explore+=data.getDataSize();
	#if LL_DEBUG<=LOGLEVEL
							std::string dbg="Sending packet route=(";
							for(size_t k=0;k<route_peers.size();++k)
							{
								dbg+=nconvert(route_peers[k]->id);
								if(k+1<route_peers.size())
									dbg+=", ";
							}
							dbg+=") ID="+nconvert(curr_msg_id-1);
							LOG(dbg, LL_DEBUG);
	#endif
							qdata.data.clear();
							msgpeers.clear();
							route_peers.clear();
							route.clear();
							latency=default_latency;
							if( b_explore>=bandwidth_exploration )
							{
								break;
							}
						}
					}
				}
				if( b_explore>=bandwidth_exploration )
				{
					break;
				}
			}

			//Erase the buffers we have already sent
			new_bufs.erase(new_bufs.begin(), new_bufs.begin()+delbufs);
			
			LOG("Exploretime: "+nconvert(os_gettimems()-explore_time), LL_DEBUG);
		}
		
		unsigned int ack_time=os_gettimems();		

		//Get the acks from the tracker
		std::vector<SAck> acks=tracker->getNewAcks();
		if(!acks.empty())
		{
			for(size_t i=0;i<acks.size();++i)
			{
				//Get the source client id
				unsigned int source_id;
				std::map<std::pair<unsigned int, unsigned short>, unsigned int>::iterator iter=peers_ids.find(std::pair<unsigned int, unsigned short>(acks[i].sourceip, acks[i].sourceport) );
				if(iter!=peers_ids.end())
				{
					source_id=iter->second;

					//Get the message by using the source and client id
					std::map<unsigned int, std::map<unsigned int, SMessage*> >::iterator it1=sent_msgs.find(acks[i].msgid);
					if(it1!=sent_msgs.end())
					{
						std::map<unsigned int, SMessage*>::iterator it2=it1->second.find(source_id);
						if(it2!=it1->second.end())
						{
							//Update the rtts of the clients on the path
							updateRtts(acks[i].rtt, it2->second);
							if(!it2->second->acked)
							{
								//Handle the ack
								handleAck(it2->second);
							}
							else
							{
								LOG("delayed ack "+nconvert(it2->second->id), LL_INFO);
							}
							it2->second->acked=true;
						}
					}
				}
			}

			//Sort the timeouts so that the oldest is the first one
			msgs_timeouts.sort();
			unsigned int c_time=os_gettimems();
			//While the oldest message is timed out...
			while(!msgs_timeouts.empty() && msgs_timeouts.front()->timeouttime<c_time)
			{
				//If it is acked delete it
				if(msgs_timeouts.front()->acked )
				{
					std::map<unsigned int, std::map<unsigned int, SMessage*> >::iterator it1=sent_msgs.find(msgs_timeouts.front()->id);
					if(it1!=sent_msgs.end() && !msgs_timeouts.front()->route.empty())
					{
						std::map<unsigned int, SMessage*>::iterator it2=it1->second.find(msgs_timeouts.front()->route[msgs_timeouts.front()->route.size()-1]);
						if(it2!=it1->second.end())
						{
							it1->second.erase(it2);
						}
						if(it1->second.empty())
						{
							sent_msgs.erase(it1);
						}
					}
					delete msgs_timeouts.front();
					msgs_timeouts.erase(msgs_timeouts.begin());
				}
				else
				{
					//Call timeout function and add the message to the garbage. It get's deleted in 10s
					LOG("ACK timeout for ID="+nconvert(msgs_timeouts.front()->id), LL_INFO);
					handleTimeout(msgs_timeouts.front());
					std::map<unsigned int, std::map<unsigned int, SMessage*> >::iterator it1=sent_msgs.find(msgs_timeouts.front()->id);
					if(it1!=sent_msgs.end() && !msgs_timeouts.front()->route.empty())
					{
						std::map<unsigned int, SMessage*>::iterator it2=it1->second.find(msgs_timeouts.front()->route[msgs_timeouts.front()->route.size()-1]);
						if(it2!=it1->second.end())
						{
							it2->second->acked=true;
						}
					}
					msgs_garbage.push(msgs_timeouts.front());
					msgs_timeouts.erase(msgs_timeouts.begin());
				}
			}

			//Delete messages in garbage that are older than 10s
			while(!msgs_garbage.empty() && os_gettimems()-msgs_garbage.front()->timeouttime>10000)
			{
				std::map<unsigned int, std::map<unsigned int, SMessage*> >::iterator it1=sent_msgs.find(msgs_garbage.front()->id);
				if(it1!=sent_msgs.end() && !msgs_garbage.front()->route.empty())
				{
					std::map<unsigned int, SMessage*>::iterator it2=it1->second.find(msgs_garbage.front()->route[msgs_garbage.front()->route.size()-1]);
					if(it2!=it1->second.end())
					{
						it1->second.erase(it2);
					}
					if(it1->second.empty())
					{
						sent_msgs.erase(it1);
					}
				}
				delete msgs_garbage.front();
				msgs_garbage.pop();
			}

			//Update the best_node structure
			{
				updateBestNodes();
			}

			LOG("Acktime: "+nconvert(os_gettimems()-ack_time), LL_DEBUG);
		}

		//Sleep until next timeslice
		if(os_gettimems()<b_next_reset)
		{
			os_sleep((std::min)((unsigned int)bandwidth_timestep,b_next_reset-os_gettimems()));
		}
		//Reset the client rates
		for(std::map<unsigned int, SPeer>::iterator it=peers.begin();it!=peers.end();++it)
		{
			it->second.c_wnd=0;
		}
		b_explore=0;
		b_exploit=0;
		b_next_reset=os_gettimems()+bandwidth_timestep;
	}
}

/**
* Returns if the buffer with it bid is spread to all clients by the current tree structure
**/
bool Controller::isSpread(size_t bid)
{
	std::map<size_t, std::vector<SBufferInfo*> >::iterator it=buffer_info.find(bid);
	if(it==buffer_info.end())
		return true;

	for(size_t i=0;i<it->second.size();++i)
	{
		if(it->second[i]->sendc<msg_redundancy)
			return false;
	}
	return true;
}

/**
* update the RTT using message msg (for which an ACK was received) and the delay from last client to server
*/
void Controller::updateRtts(float server_rtt, SMessage *msg)
{
	//Estimate latency by substracting the latency from last node to server and dividing by the hopcount
	float est=(((float)(os_gettimems()-msg->senttime)/1000.f)-server_rtt/2.f)/(float)msg->route.size();
	if(est>0)
	{
		//Update the latency in both directions
		for(size_t i=0;i+1<msg->route.size();++i)
		{
			updateSingleLatency(msg->route[i], msg->route[i+1], est);
			updateSingleLatency(msg->route[i+1], msg->route[i], est);
		}
		//Update the server_rtt
		if(!msg->route.empty())
		{
			std::map<unsigned int, SPeer>::iterator it=peers.find(msg->route[msg->route.size()-1]);
			if(it!=peers.end())
			{
				it->second.server_rtt=server_rtt;
			}
		}
	}
	else
	{
		LOG("RTT estimate negativ", LL_DEBUG);
	}
}

/**
* update the latency from 'from' to 'to' with newrtt
**/
void Controller::updateSingleLatency(unsigned int from, unsigned int to, float newrtt)
{
	std::map<unsigned int, SPeer>::iterator it1=peers.find(from);
	if(it1!=peers.end())
	{
		std::map<unsigned int, SRtt>::iterator it2=it1->second.latencies.find(to);
		if(it2!=it1->second.latencies.end())
		{
			//Update the mean and variance
			float err=newrtt-it2->second.mean;
			it2->second.mean+=rttalpha*err;
			if(err<0)err*=-1;
			it2->second.var+=rttalpha*(err-it2->second.var);
		}
		else
		{
			//Set the mean and variance because it was never updated before
			SRtt rtt;
			rtt.mean=newrtt;
			rtt.var=newrtt;
			it1->second.latencies.insert(std::pair<unsigned int, SRtt>(to, rtt) );
		}
	}
}

/**
* update the data structure about the best nodes
**/
void Controller::updateBestNodes(void)
{
	for(std::map<unsigned int, SPeer>::iterator it=peers.begin();it!=peers.end();++it)
	{
		SBest *cbest=NULL;
		//Get the entry in the best node structure
		for(size_t i=0;i<best_nodes.size();++i)
		{
			if(best_nodes[i].peer_id==it->first)
			{
				cbest=&best_nodes[i];
				break;
			}
		}
		
		unsigned int bfree=(unsigned int)it->second.curr_state;
		if(cbest==NULL)
		{
			//Add new entry
			SBest nb;
			nb.peer_id=it->first;
			nb.used_msgs=0;
			nb.free_msgs=bfree;
			nb.free_msgs_var=0;
			nb.ip=it->second.ip;
			nb.port=it->second.port;
			nb.s=it->second.s;
			nb.id=it->second.id;
			nb.latencies=it->second.latencies;
			nb.server_rtt=it->second.server_rtt;
			best_nodes.push_back(nb);
		}
		else
		{
			//Update entries
			cbest->free_msgs=bfree;
			cbest->latencies=it->second.latencies;
			cbest->server_rtt=it->second.server_rtt;
		}
	}
	//Sort so the best node is in front
	std::sort(best_nodes.begin(), best_nodes.end());
	//Update the public structure
	boost::mutex::scoped_lock lock(mutex);
	l_best_nodes=best_nodes;
}

/**
* Get information about all peers
**/
std::vector<SBest> Controller::getBestNodes(void)
{
	boost::mutex::scoped_lock lock(mutex);
	return l_best_nodes;
}

/**
* Returns the esimated latency from client with id 'from' to client with id 'to'
**/
float Controller::getLatency(unsigned int from, unsigned int to)
{
	std::map<unsigned int, SPeer>::iterator it1=peers.find(from);
	if(it1!=peers.end())
	{
		std::map<unsigned int, SRtt>::iterator it2=it1->second.latencies.find(to);
		if(it2!=it1->second.latencies.end())
		{
			return it2->second.mean+4*it2->second.var;
		}
	}
	return default_latency;
}

/**
* Handle the timeout of message msg
**/
void Controller::handleTimeout(SMessage* msg)
{
	for(size_t i=0;i<msg->route.size()-1 || (i==0 && msg->route.size()==1);++i)
	{
		const unsigned int &target=msg->route[i];
		std::map<unsigned int, SPeer>::iterator it=peers.find(target);
		if(it!=peers.end())
		{
			//Client is in fast start mode
			if(it->second.last_cong_state==-1)
			{
				int next_state=msg->qdata.data[i].state/2;
				log("Packet loss. Leaving faststart. New rate="+nconvert(next_state));
				//Set states after leaving faststart. See thesis for details
				for(int j=msg->qdata.data[i].state/2,s=(int)it->second.qtable.size();j<s && j<msg->qdata.data[i].state;++j)
				{
					it->second.qtable[j].up=0;
					it->second.qtable[j].stay=0;
					it->second.qtable[j].down=0;
				}
				for(int j=msg->qdata.data[i].state,s=(int)it->second.qtable.size();j<s;++j)
				{
					it->second.qtable[j].up=0;
					it->second.qtable[j].stay=0;
					it->second.qtable[j].down=-1*reinf_down*j;
				}
				it->second.curr_state=next_state;
				it->second.last_cong_state=msg->qdata.data[i].state;
				continue;
			}
			it->second.last_cong_state=msg->qdata.data[i].state;
			int state=it->second.curr_state+1;
			if(it->second.last_cong_state!=-1)
			{
				//If not in faststart mode add reward
				int r_state=msg->qdata.data[i].state-msg->qdata.data[i].action;
				it->second.qtable[r_state].rewards.push_back(SActionReward(msg->qdata.data[i].action, reinf_down*state));
			}
			//eventually update q-state
			addAckPacket(&it->second);
		}
	}
}

/**
 * Learn the Q-Value of state 'state' and action 'action' as 'ret'
 */
void Controller::reinforceQValue(SPeer *pi, int state, int action, double ret)
{
	//see thesis for details
	int last_state=state-action;

	double qmax=pi->qtable[state].down;
	if(pi->qtable[state].up>qmax)
		qmax=pi->qtable[state].up;
	if(pi->qtable[state].stay>qmax)
		qmax=pi->qtable[state].stay;

	if(action==-1)
		pi->qtable[last_state].down+=alpha*(ret+lambda*qmax-pi->qtable[last_state].down);
	else if(action==1)
		pi->qtable[last_state].up+=alpha*(ret+lambda*qmax-pi->qtable[last_state].up);
	else if(action==0)
		pi->qtable[last_state].stay+=alpha*(ret+lambda*qmax-pi->qtable[last_state].stay);
}

/**
* Update the congestion controll with an received ack
*/
void Controller::addAckPacket(SPeer *pi)
{
	pi->ack_packets+=1;
	if(pi->last_cong_state==-1) // fast start
	{
		if(pi->ack_packets>(std::max)((unsigned int)10, (unsigned int)(0.1f*(float)pi->curr_state+0.5f)))
		{
			log("Fast Start: Incrementing rate");
			int next_state=2*pi->curr_state;
			if(pi->curr_state==0)
				next_state=1;
			//sthresh is the rate the client published. Do faststart until that rate or packet
			//loss
			if(next_state>pi->sthresh && pi->sthresh>0)
			{
				next_state=pi->sthresh;
				pi->last_cong_state=pi->curr_state;
			}

			//set all q-values beneath the current state to specific values
			for(int s=pi->curr_state;s<next_state && s<(int)pi->qtable.size();++s)
			{
				pi->qtable[s].up=reinf_up*(s+1);
				pi->qtable[s].down=0;
				pi->qtable[s].stay=0;
			}

			pi->curr_state=next_state;

			//We've reached maximum bandwidth
			if(pi->curr_state>=(int)pi->qtable.size())
			{
				pi->last_cong_state=pi->curr_state;
				pi->curr_state=pi->qtable.size()-1;
			}
			
			pi->ack_packets=0;

			log("New state: "+nconvert(pi->curr_state));
		}
	}
	else
	{
		if(pi->ack_packets>=(std::max)((unsigned int)10, (unsigned int)(0.1f*(float)pi->curr_state+0.5f)))
		{
			pi->ack_packets=0;
			//update the q-state
			updateQValue(pi);
			log("StateUpdate: s="+nconvert(pi->curr_state));
		}
	}
}

/**
 * Select the next state epsilon-greedy on policy (state with maximum q-value
 */
void Controller::updateQValue(SPeer *pi)
{
	//Apply rewards
	size_t reward_size=pi->qtable[pi->curr_state].rewards.size();
	if(reward_size>0)
	{
		SQValues reward;
		size_t rewards_up=0;
		size_t rewards_down=0;
		size_t rewards_stay=0;
		for(size_t i=0;i<reward_size;++i)
		{
			SActionReward &r=pi->qtable[pi->curr_state].rewards[i];
			if(r.action==-1)
			{
				reward.down+=r.value;
				++rewards_down;
			}
			else if(r.action==0)
			{
				reward.stay+=r.value;
				++rewards_stay;
			}
			else if(r.action==1)
			{
				reward.up+=r.value;
				++rewards_up;
			}
		}

		if(rewards_up>0)
		{
			reinforceQValue(pi, pi->curr_state+1, 1, reward.up/(double)rewards_up);
		}
		if(rewards_stay>0)
		{
			reinforceQValue(pi, pi->curr_state, 0, reward.stay/(double)rewards_stay);
		}
		if(rewards_down>0)
		{
			reinforceQValue(pi, pi->curr_state-1, -1, reward.down/(double)rewards_down);
		}

		pi->qtable[pi->curr_state].rewards.clear();
	}

	//Select next action - see thesis for details (e-greedy and on policy)
	float r=(float)rand()/(float)RAND_MAX;
	if(r<epsilon)
	{
		pi->last_action=rand()%3-1;
	}
	else
	{
		double qmax=pi->qtable[pi->curr_state].down;
		pi->last_action=-1;
		if(pi->qtable[pi->curr_state].up==qmax)
		{
			pi->last_action=rand()%2;
			if(pi->last_action==0)
				pi->last_action=-1;
		}
		if(pi->qtable[pi->curr_state].up>qmax)
		{
			pi->last_action=1;
			qmax=pi->qtable[pi->curr_state].up;
		}
		if(pi->qtable[pi->curr_state].stay>qmax)
		{
			pi->last_action=0;
			qmax=pi->qtable[pi->curr_state].up;
		}
	}

	if(pi->last_action==-1)
	{
		if(pi->curr_state>0)
		{
			--pi->curr_state;
		}
		else
		{
			//pi->curr_state; do nothing
			pi->last_action=0;
		}
	}
	else if(pi->last_action==1)
	{
		if((unsigned int)pi->curr_state<pi->qtable.size()-1)
		{
			++pi->curr_state;
		}
		else
		{
			//--pi->curr_state; do nothing
			pi->last_action=0;
		}
	}
}

/**
* Handle an acknowledgement of message msg
**/
void Controller::handleAck(SMessage* msg)
{
	if(msg->route.size()>1)
	{
		//Add reward for each peer on the route
		for(size_t i=0;i<msg->route.size()-1;++i)
		{
			const unsigned int &target=msg->route[i];
			std::map<unsigned int, SPeer>::iterator it=peers.find(target);
			if(it!=peers.end())
			{
				int state=it->second.curr_state+1;
				if(it->second.last_cong_state!=-1)
				{
					int r_state=msg->qdata.data[i].state-msg->qdata.data[i].action;
					it->second.qtable[r_state].rewards.push_back(SActionReward(msg->qdata.data[i].action, reinf_up*state));
				}
				addAckPacket(&it->second);
			}
		}
	}
	else
	{
		//Add reward for the one peer we sent the message to
		const unsigned int &target=msg->route[0];
		std::map<unsigned int, SPeer>::iterator it=peers.find(target);
		if(it!=peers.end())
		{
			int state=it->second.curr_state+1;
			if(it->second.last_cong_state!=-1)
			{
				int r_state=msg->qdata.data[0].state-msg->qdata.data[0].action;
				it->second.qtable[r_state].rewards.push_back(SActionReward(msg->qdata.data[0].action, reinf_up*state));
			}
			addAckPacket(&it->second);
		}
	}
}