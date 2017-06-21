#ifdef _WIN32
#include <windows.h>
#endif

#include "tracker.h"
#include "../common/log.h"
#include "../common/packet_ids.h"
#include "../common/stringtools.h"
#include "../common/msg_tree.h"
#include "../common/msg_ack.h"
#include <queue>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <stack>

//How often should a client get each message via the trees?
const int spread_redunancy=1;
//Size of a message. Usually mtu
const unsigned int msgsize=1500;
//Number of optimization iterations per tree update
const unsigned int opt_iters=20;
//Percentage of bandwidth with which the tracker should calculate.
//Make this smaller, so there's some tolerance to changing streaming
//rates
const float bandwidth_pc=0.5f;

/**
* Initialize tracker by setting the port it should listen on and the bandwidth it can use for the trees
**/
Tracker::Tracker(unsigned short pPort, unsigned int exploit_bandwidth) : port(pPort), t_exploit_bandwidth(exploit_bandwidth)
{
	last_spread_update=os_gettimems();
	controller=NULL;
	SBest *data=new SBest;
	data->free_msgs=(unsigned int)(((float)exploit_bandwidth*bandwidth_pc)/(float)msgsize+0.5f);
	data->used_msgs=0;
	data->id=0;
	for(int i=0;i<k_slices;++i)
	{
		STreeNode *root=new STreeNode;
		root->parent=NULL;
		root->data=data;
		root->k=i;
		root->root_latency=0;
		roots.push_back(root);
	}

	viz_trees=false;
}

/**
* Update the trees, to accomodate new and changed clients
**/
void Tracker::update_spreads(void)
{
	while(true)
	{
		os_sleep(1000);
		if(os_gettimems()-last_spread_update>10000)
		{
			boost::mutex::scoped_lock lock(tree_mutex);
			updateSpread();
		}
	}
}

/**
* Main thread function
**/
void Tracker::operator()(void)
{
#ifdef _WIN32
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

	server_socket=os_createSocket(false);
	if(!os_bind(server_socket, port))
	{
		log("error binding socket to port "+nconvert(port));
		return;
	}

	os_listen(server_socket, 1000);

	/*boost::thread spread_thread(boost::bind( &Tracker::update_spreads, this ); );
	spread_thread.yield();*/

	clients.push_back(server_socket);
	while(true)
	{
		std::vector<SOCKET> ready_clients=os_select(clients, 1000);
		std::vector<SOCKET> dels;

		for(size_t i=0;i<ready_clients.size();++i)
		{
			if(ready_clients[i]==server_socket)
			{
				unsigned int nip;
				SOCKET ns=os_accept(server_socket, &nip);
				if(ns!=SOCKET_ERROR)
				{
					os_nagle(ns, false);
					clients.push_back(ns);
					SClientData ncd;
					ncd.lastpingtime=os_gettimems();
					ncd.lastpong=ncd.lastpingtime;
					ncd.s=ns;
					ncd.ip=nip;
					client_data[ns]=ncd;
					log("New client. Clients: "+nconvert(client_data.size()));
				}
			}
			else
			{
				SOCKET s=ready_clients[i];
				char buffer[4096];
				int rc=os_recv(s, buffer, 4096);
				if(rc<=0)
				{
					log("Error in client. Removing client.");
					removeClient(s);
				}
				else
				{
					std::map<SOCKET, SClientData>::iterator it=client_data.find(s);
					if(it!=client_data.end())
					{
						it->second.tcpstack.AddData(buffer, rc);
						size_t msgsize;
						char *buf;
						while( (buf=it->second.tcpstack.getPacket(&msgsize))!=NULL)
						{
							CRData data(buf, msgsize);
							receivePacket(&it->second, data);
							delete []buf;
						}
					}
				}
			}
		}

		for(std::map<SOCKET, SClientData>::iterator it=client_data.begin();it!=client_data.end();++it)
		{
			unsigned int ctime=os_gettimems();
			if(ctime-it->second.lastpingtime>1000)
			{
				CWData data;
				data.addUChar(TRACKER_PING);
				it->second.tcpstack.Send(it->second.s, data);
				it->second.lastpingtime=os_gettimems();
				LOG("Sending PING", LL_DEBUG);
			}
			if(ctime-it->second.lastpong>10000)
			{
				log("Client timeout. Removing client.");
				os_closesocket(it->second.s);
				dels.push_back(it->second.s);
			}
		}

		for(size_t i=0;i<dels.size();++i)
		{
			boost::mutex::scoped_lock lock(tree_mutex);
			removeClient(dels[i]);
		}

		if(os_gettimems()-last_spread_update>100)
		{
			last_spread_update=os_gettimems();
			boost::mutex::scoped_lock lock(tree_mutex);
			updateSpread();

			if(viz_trees==true)
			{
				viz_trees=false;
				drawTrees();
			}
		}
	}
}

/**
* remove a client from the tracker specified by socket 's'
**/
void Tracker::removeClient(SOCKET s)
{
	for(size_t i=0;i<clients.size();++i)
	{
		if(clients[i]==s)
		{
			clients.erase(clients.begin()+i);
			break;
		}
	}

	std::map<SOCKET, SClientData>::iterator it=client_data.find(s);
	if(it!=client_data.end())
	{
		if(it->second.port!=0)
		{
			boost::mutex::scoped_lock lock(mutex);
			SNewClient nc;
			nc.ip=it->second.ip;
			nc.port=it->second.port;
			nc.s=it->first;
			exit_clients.push_back( nc );
			bool first=true;
			for(size_t j=0;j<it->second.treenodes.size();++j)
			{
				STreeNode *tn=it->second.treenodes[j];
				{
					std::map<STreeNode*, bool>::iterator it=opt_update.find(tn);
					if(it!=opt_update.end())opt_update.erase(it);
				}
				if(tn->parent!=NULL)
				{
					--tn->parent->data->used_msgs;
					opt_update[tn->parent]=true;
					detachChild(tn);
				}
				else
				{
					for(size_t i=0;i<unasignable_nodes.size();++i)
					{
						if(unasignable_nodes[i]==tn)
						{
							unasignable_nodes.erase(unasignable_nodes.begin()+i);
							break;
						}
					}
				}
				for(size_t l=0;l<tn->children.size();++l)
				{
					STreeNode *child=tn->children[l];
					child->parent=NULL;
					bool b=addExistingNode(roots[child->k], child);
					if(!b)
					{
						child->parent=NULL;
						unasignable_nodes.push_back(child);
						log("Reassigning a child failed after a client left the server");
					}
				}
				if(first)
				{
					std::map<unsigned int, SBest*>::iterator it=nodes_info.find(tn->data->id);
					if(it!=nodes_info.end())
					{
						nodes_info.erase(it);
					}
					delete tn->data;
					first=false;
				}
				delete tn;
			}
		}
		client_data.erase(it);
	}
}

/**
* handle a new packet with data 'data' from client with clientdata 'cd'
**/
void Tracker::receivePacket( SClientData *cd, CRData &data)
{
	uchar id;
	if(!data.getUChar(&id))
		return;

	switch(id)
	{
	case TRACKER_PONG:
		{
			cd->lastpong=os_gettimems();
			float delay=(float)(cd->lastpong-cd->lastpingtime)/1000.f;
			float err=delay-cd->rtt;
			if(cd->rtt!=-1)
				cd->rtt+=0.1f*err;
			else
				cd->rtt=delay;

			LOG("Received PONG: New delay:"+nconvert(cd->rtt), LL_DEBUG);
		}break;
	case TRACKER_PORT:
		{
			unsigned short np;
			unsigned int bandwidth;
			if(data.getUShort(&np) && data.getUInt(&bandwidth))
			{
				if(cd->port==0)
				{
					cd->port=np;
					boost::mutex::scoped_lock lock(mutex);
					SNewClient nc;
					nc.ip=cd->ip;
					nc.port=cd->port;
					nc.s=cd->s;
					nc.bandwidth=bandwidth;
					new_clients.push_back(nc);
				}
			}
		}break;
	case TRACKER_ACK:
		{
			msg_ack ack(data);
			SAck na;
			LOG("ACK for ID="+nconvert(ack.getMsgID()),LL_DEBUG);
			na.msgid=ack.getMsgID();
			na.sourceip=ack.getSourceIP();
			na.sourceport=ack.getSourcePort();
			na.rtt=cd->rtt==-1.f?0.5f:cd->rtt;
			boost::mutex::scoped_lock lock(mutex);
			new_acks.push_back(na);
		}break;
	case TRACKER_NACK:
		{
			unsigned int id;
			if(data.getUInt(&id))
			{
				LOG("NACK for ID="+nconvert(id), LL_INFO);
				SResend r;
				r.ip=cd->ip;
				r.port=cd->port;
				r.msgid=id;
				boost::mutex::scoped_lock lock(mutex);
				new_resends.push_back(r);
			}
		}break;
	};
}

/**
* Functions for connecting this thread with the other two threads
**/ 
void Tracker::setController(Controller *pController)
{
	controller=pController;
}

void Tracker::setInput(Input *pInput)
{
	input=pInput;
}

/**
* Do the tree optimizations and send the modifications to the clients
**/
void Tracker::updateSpread(void)
{
	std::vector<SBest> best=controller->getBestNodes();

	float packets_second=input->getMaxPacketsPerSecond();
	float input_k=packets_second/(float)k_slices;

	if(!roots.empty() && packets_second!=0)
	{
		roots[0]->data->free_msgs=(unsigned int)((((float)t_exploit_bandwidth*bandwidth_pc)/(float)msgsize)/(float)input_k+0.5f);
	}

	for(size_t k=0;k<best.size();++k)
	{
		std::map<unsigned int, SBest*>::iterator it=nodes_info.find(best[k].id);
		if(it!=nodes_info.end())
		{
			it->second->free_msgs=(unsigned int)((float)best[k].free_msgs/(float)input_k);
			it->second->latencies=best[k].latencies;
			it->second->server_rtt=best[k].server_rtt;
		}
		else
		{
			SBest *data=new SBest(best[k]);
			std::map<SOCKET, SClientData>::iterator it=client_data.find(data->s);
			if(it!=client_data.end())
			{
				bool b=addNewNode(data, &it->second);
				nodes_info[data->id]=data;
			}
		}
	}

	for(size_t i=0;i<roots.size();++i)
	{
		enforceConstraints(roots[i], NULL);
	}

	for(size_t i=0;i<opt_iters;++i)
	{
		int r=rand()%roots.size();
		modifyTree(roots[r]);
		optimizeTree(roots[r]);
	}

	if(!unasignable_nodes.empty())
	{
		int rf=0;
		for(size_t i=0;i<roots.size();++i)
		{
			if(roots[i]->children.empty())
				++rf;
		}
		for(size_t i=0;i<roots.size() && rf>0;++i)
		{
			if(makeRootFree(roots[i]))
				--rf;
		}
	}	

	for(int i=0;i<(int)unasignable_nodes.size();++i)
	{
		if(addExistingNode(roots[unasignable_nodes[i]->k], unasignable_nodes[i]) )
		{
			unasignable_nodes.erase(unasignable_nodes.begin()+i);
			--i;
		}
	}

	if(!unasignable_nodes.empty())
		log(nconvert(unasignable_nodes.size())+" unasignable nodes still exist");

	for(std::map<STreeNode*, bool>::iterator it=opt_update.begin();it!=opt_update.end();++it)
	{
		sendSpread(it->first);
	}



	opt_update.clear();
}

/**
* Add a new nodes to every tree using data 'nn' and 'cd'
**/
bool Tracker::addNewNode(SBest *nn, SClientData *cd)
{
	bool ok=true;
	for(size_t k=0;k<roots.size();++k)
	{
		bool b=addNewNodeSlice(roots[k], nn, cd);
		if(!b)
		{
			STreeNode *tn=new STreeNode;
			cd->treenodes.push_back(tn);
			tn->parent=NULL;
			tn->data=nn;
			tn->k=k;
			unasignable_nodes.push_back(tn);
			log("Adding new node to slice "+nconvert(k)+" failed.");
			ok=false;
		}
	}
	return ok;
}

/**
* Add existing node 'curr' to the tree specified by root 'root'.
* 'curr' can have children.
**/
bool Tracker::addExistingNode(STreeNode* root, STreeNode *curr)
{
	if(root->data->free_msgs>root->data->used_msgs)
	{
		++root->data->used_msgs;
		root->children.push_back(curr);
		curr->parent=root;

		if(root->parent!=NULL)
		{
			opt_update[root]=true;
		}
		return true;
	}
	else
	{
		for(size_t i=0;i<root->children.size();++i)
		{
			if(addExistingNode(root->children[i], curr) )
				return true;
		}
		return false;
	}
}

/**
* Add a new node to the tree specified by its root 'root'
**/
bool Tracker::addNewNodeSlice(STreeNode* root, SBest *nn, SClientData *cd)
{
	if(root->data->free_msgs>root->data->used_msgs)
	{
		STreeNode *tn=new STreeNode;
		cd->treenodes.push_back(tn);
		tn->parent=root;
		tn->data=nn;
		tn->k=root->k;
		root->children.push_back(tn);
		++root->data->used_msgs;

		if(root->parent!=NULL)
		{
			opt_update[root]=true;
		}
		return true;
	}
	else
	{
		for(size_t i=0;i<root->children.size();++i)
		{
			if(addNewNodeSlice(root->children[i], nn, cd) )
				return true;
		}
		return false;
	}
}

/**
* Optimize the tree starting with 'root' as root node
**/
void Tracker::optimizeTree(STreeNode *root)
{
	std::vector<STreeNode*> nodes=getTreeNodes(root);
	STreeNode *opt_first=NULL;
	STreeNode *opt_second=NULL;
	float opt_perf=evaluateTreePerformance(root);
	for(size_t i=0;i<nodes.size();++i)
	{
		for(size_t j=i+1;j<nodes.size();++j)
		{
			if((int)nodes[i]->data->free_msgs-(int)nodes[i]->data->used_msgs-(int)nodes[i]->children.size()>=(int)nodes[j]->children.size()
				&& ((int)nodes[j]->data->free_msgs-(int)nodes[j]->data->used_msgs-(int)nodes[j]->children.size())>=(int)nodes[i]->children.size())
			{
				switchNodes(nodes[i], nodes[j]);
				float perf=evaluateTreePerformance(root);
				if(perf<opt_perf)
				{
					opt_perf=perf;
					opt_first=nodes[i];
					opt_second=nodes[j];
				}
				switchNodes(nodes[j], nodes[i]);
			}
		}
	}

	if(opt_first!=NULL)
	{
		if(opt_first->parent->parent!=NULL)
			opt_update[opt_first->parent]=true;
		if(opt_second->parent->parent!=NULL)
			opt_update[opt_second->parent]=true;

		opt_first->data->used_msgs-=(unsigned int)opt_first->children.size();
		opt_second->data->used_msgs-=(unsigned int)opt_second->children.size();

		opt_update[opt_first]=true;
		opt_update[opt_second]=true;

		switchNodes(opt_first, opt_second);

		opt_first->data->used_msgs+=(unsigned int)opt_first->children.size();
		opt_second->data->used_msgs+=(unsigned int)opt_second->children.size();

		if(opt_first->parent==opt_first || opt_second->parent==opt_second )
			log("Tree construction error1");
	}
}

/**
* Enforce the current client bandwidths in the tree with root node 'root'. curr denotes the current
* Node that is being processed.
**/
void Tracker::enforceConstraints(STreeNode *root, STreeNode *curr)
{
	if(curr==NULL) curr=root;

	while(!curr->children.empty() && curr->data->free_msgs<curr->data->used_msgs)
	{
		int r=rand()%curr->children.size();
		STreeNode *l=curr->children[r];
		curr->children.erase(curr->children.begin()+r);
		l->parent=NULL;
		--curr->data->used_msgs;
		opt_update[curr]=true;
		if(!addExistingNode(root, l))
		{
			l->parent=NULL;
			unasignable_nodes.push_back(l);
			log("reassinging node failed!");
		}
	}

	for(size_t i=0;i<curr->children.size();++i)
	{
		enforceConstraints(root, curr->children[i]);
	}
}

/**
* If possible remove children from the root and add them somewhere else
**/
bool Tracker::makeRootFree(STreeNode *root)
{
	for(size_t i=0;i<root->children.size();++i)
	{
		STreeNode *child=root->children[i];
		detachChild(child);
		--root->data->used_msgs;
		for(size_t j=0;j<root->children.size();++j)
		{
			if(addExistingNode(root->children[j], child) )
			{
				return true;
			}
		}
		++root->data->used_msgs;
		attachChild(root, child);
	}
	return false;
}

/**
* Optimize the tree by allowing nodes to adopt other nodes
**/
void Tracker::modifyTree(STreeNode *root)
{
	std::vector<STreeNode*> nodes=getTreeNodes(root);
	STreeNode *opt_first=NULL;
	STreeNode *opt_second=NULL;
	float opt_perf=evaluateTreePerformance(root);
	for(size_t i=0;i<nodes.size();++i)
	{
		if(nodes[i]->data->free_msgs>nodes[i]->data->used_msgs)
		{
			STreeNode *pnode=nodes[i];
			for(size_t j=0;j<nodes.size();++j)
			{
				if(i==j) continue;

				STreeNode *cnode=nodes[j];
				STreeNode *l_parent=cnode->parent;
				if(l_parent==pnode)
					continue;
				if(pnode->parent==cnode)
					continue;
				STreeNode *c=pnode->parent;
				while(c!=NULL && c!=cnode)
					c=c->parent;

				if(c==cnode)
					continue;

				detachChild(cnode);
				attachChild(pnode, cnode);

				if(cnode->parent==cnode || pnode->parent==pnode )
						log("Tree construction error1");
				float perf=evaluateTreePerformance(root);
				if(perf<opt_perf)
				{
					opt_perf=perf;
					opt_first=nodes[i];
					opt_second=nodes[j];
				}

				detachChild(cnode);
				attachChild(l_parent, cnode);
			}
		}
	}

	if(opt_first!=NULL)
	{
		if(opt_second->parent->parent!=NULL)
			opt_update[opt_second->parent]=true;

		opt_update[opt_first]=true;

		STreeNode *tparent=opt_second->parent;
		detachChild(opt_second);
		tparent->data->used_msgs-=1;

		attachChild(opt_first, opt_second);
		opt_first->data->used_msgs+=1;

		if(opt_first->parent==opt_first || opt_second->parent==opt_second )
			log("Tree construction error1");
	}
}

/**
* Detach a node from its parent
**/
void Tracker::detachChild(STreeNode *child)
{
	for(size_t i=0;i<child->parent->children.size();++i)
	{
		if(child->parent->children[i]==child)
		{
			child->parent->children.erase(child->parent->children.begin()+i);
			break;
		}
	}

	child->parent=NULL;
}

/**
* Attach node 'new_child' to 'parent'. Afterwards 'parent' is 'new_child''s parent
**/
void Tracker::attachChild(STreeNode *parent, STreeNode *new_child)
{
	parent->children.push_back(new_child);
	new_child->parent=parent;
}

/**
* Exchange the nodes n1 and n2. That means n2 has then n1's parent as parent and
* vise versa, as well as n2 has n1's children.
**/
void Tracker::switchNodes(STreeNode *n1, STreeNode *n2)
{
	if(n1->parent==n2 || n2->parent==n1)
	{
		if(n2->parent==n1)
		{
			STreeNode *t;
			t=n1;
			n1=n2;
			n2=t;
		}

		STreeNode *parent=n2->parent;
		for(size_t l=0;l<parent->children.size();++l)
		{
			if(parent->children[l]==n2)
			{
				parent->children[l]=n1;
				break;
			}
		}
		n1->parent=n2->parent;

		std::vector<STreeNode*> tmp=n2->children;
		for(size_t i=0;i<tmp.size();++i)
		{
			if(tmp[i]==n1)
				tmp[i]=n2;
			tmp[i]->parent=n1;
		}
		n2->children=n1->children;
		for(size_t i=0;i<n2->children.size();++i)
		{
			n2->children[i]->parent=n2;
		}
		n1->children=tmp;
		n2->parent=n1;
	}
	else
	{
		STreeNode *n1_parent=n1->parent;
		{
			STreeNode *parent=n1->parent;
			for(size_t l=0;l<parent->children.size();++l)
			{
				if(parent->children[l]==n1)
				{
					parent->children[l]=n2;
					break;
				}
			}
			n1->parent=n2->parent;
		}
		{
			std::vector<STreeNode*> tmp=n1->children;
			for(size_t i=0;i<tmp.size();++i)
			{
				tmp[i]->parent=n2;
			}
			n1->children=n2->children;
			for(size_t i=0;i<n1->children.size();++i)
			{
				n1->children[i]->parent=n1;
			}
			n2->children=tmp;
		}
		{
			STreeNode *parent=n2->parent;
			for(size_t l=0;l<parent->children.size();++l)
			{
				if(parent->children[l]==n2)
				{
					parent->children[l]=n1;
					break;
				}
			}
			n2->parent=n1_parent;
		}
	}
}

/**
* Return a measure of the performance tree with root 'root' has. Smaller is better
**/
float Tracker::evaluateTreePerformance(STreeNode *root)
{
	float avg=0;
	std::stack<STreeNode*> st;
	st.push(root);
	while(!st.empty())
	{
		STreeNode *curr=st.top();
		st.pop();
		if(curr!=root)
		{
			if(curr->parent->parent==NULL)
			{
				curr->root_latency=curr->data->server_rtt/2.f;
				avg+=curr->root_latency;
			}
			else
			{
				std::map<unsigned int, SRtt>::iterator it=curr->data->latencies.find(curr->parent->data->id);
				if(it!=curr->data->latencies.end())
				{
					curr->root_latency=curr->parent->root_latency+it->second.mean;
					avg+=curr->root_latency;				
				}
			}
		}
		for(size_t i=0;i<curr->children.size();++i)
		{
			st.push(curr->children[i]);
		}
	}
	return avg;
}

/**
* Get a list of all tree nodes in the tree defined by 'root'
**/
std::vector<STreeNode*> Tracker::getTreeNodes(STreeNode *root)
{
	std::vector<STreeNode*> ret;
	for(size_t i=0;i<root->children.size();++i)
	{
		ret.push_back(root->children[i]);
		std::vector<STreeNode*> n=getTreeNodes(root->children[i]);
		ret.insert(ret.end(), n.begin(), n.end());
	}
	return ret;
}

/**
* Send to the node 'curr' which children it has in that tree - to whom it has to relay
* messages if they're in this tree
**/
void Tracker::sendSpread(STreeNode *curr)
{
	std::vector<std::pair<unsigned int,unsigned short> > children;
	for(size_t i=0;i<curr->children.size();++i)
	{
		children.push_back(std::pair<unsigned int,unsigned short>(curr->children[i]->data->ip, curr->children[i]->data->port) );
	}
	for(size_t i=0;i<curr->ref_nodes.size();++i)
	{
		children.push_back(std::pair<unsigned int,unsigned short>(curr->ref_nodes[i]->data->ip, curr->ref_nodes[i]->data->port) );
	}

	msg_tree msg(children, curr->k, k_slices);
	CWData data;
	msg.getMessage(data);
	stack.Send(curr->data->s, data);
}

/**
* Get spread information starting with tree node 'curr'
**/
std::vector<SSpread> Tracker::getSpreadNodes(STreeNode *curr)
{
	std::vector<SSpread> ret;

	SSpread s;
	s.forward=!(curr->children.empty() || curr->ref_nodes.empty() );
	s.id=curr->data->id;
	s.load=curr->children.size()+curr->ref_nodes.size();
	if(curr->parent==NULL)
		s.child=false;
	else if(curr->parent->parent==NULL)
		s.child=true;
	else
		s.child=false;
	
	ret.push_back(s);

	for(size_t i=0;i<curr->children.size();++i)
	{
		std::vector<SSpread> nl=getSpreadNodes(curr->children[i]);
		ret.insert(ret.end(), nl.begin(), nl.end());
	}

	for(size_t i=0;i<curr->ref_nodes.size();++i)
	{
		std::vector<SSpread> nl=getSpreadNodes(curr->ref_nodes[i]);
		ret.insert(ret.end(), nl.begin(), nl.end());
	}

	return ret;
}

/**
* Get spread information about a data slice k
**/
std::vector<SSpread> Tracker::getSpreadNodes(int k)
{
	boost::mutex::scoped_lock lock(tree_mutex);
	return getSpreadNodes(roots[k]);
}

/**
* Get new clients
**/
std::vector<SNewClient> Tracker::getNewClients(void)
{
	boost::mutex::scoped_lock lock(mutex);
	std::vector<SNewClient> ret=new_clients;
	new_clients.clear();
	return ret;
}

/**
* Get newly disconnected clients
**/
std::vector<SNewClient> Tracker::getExitClients(void)
{
	boost::mutex::scoped_lock lock(mutex);
	std::vector<SNewClient> ret=exit_clients;
	exit_clients.clear();
	return ret;
}

/**
* Get new acks
**/
std::vector<SAck> Tracker::getNewAcks(void)
{
	boost::mutex::scoped_lock lock(mutex);
	std::vector<SAck> ret=new_acks;
	new_acks.clear();
	return ret;
}

/**
* Get new resends
**/
std::vector<SResend> Tracker::getNewResends(void)
{
	boost::mutex::scoped_lock lock(mutex);
	std::vector<SResend> ret=new_resends;
	new_resends.clear();
	return ret;	
}

/**
* Visualize the trees
**/
void Tracker::visualize_trees(void)
{
	viz_trees=true;
}

/**
* Draw the trees
**/
void Tracker::drawTrees(void)
{
	size_t start=0;
	do
	{
		std::string data="digraph finite_state_machine {\nnode [shape = circle];size=\"7.08661417,8.66141732\"\n";
		for(size_t i=start;i<roots.size() && i-start<10;++i)
		{
			data+=drawTree(roots[i]);
		}
		data+="\n}";
		writestring(data,"trees"+nconvert(start/10)+".viz");
		start+=10;
	}
	while(start<roots.size());
}

/**
* Draw the tree
**/
std::string Tracker::drawTree(STreeNode *n)
{
	std::string r;
	for(size_t i=0;i<n->children.size();++i)
	{
		float latency=0.5;
		std::map<unsigned int, SRtt>::iterator it=n->data->latencies.find(n->children[i]->data->id);
		if(it!=n->data->latencies.end())
		{
			latency=it->second.mean;
		}
		else if(n->parent==NULL)
		{
			latency=n->children[i]->root_latency;
		}
		r+="\""+nconvert(n->data->id)+"_"+nconvert(n->k)+"\" -> \""+nconvert(n->children[i]->data->id)+"_"+nconvert(n->children[i]->k)+"\"\n"; //[ label = \""+nconvert(latency)+"\" ];
		r+=drawTree(n->children[i]);
	}
	return r;
}