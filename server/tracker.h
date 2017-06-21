/**
* Tracker thread.
* Opens a TCP port to which the clients initially connect to and announce the port on
* which they want to receive the UDP streaming packets. The tracker thread regularly sends pings to its
* clients. If a pong isn’t receive in time the client is assumed to be dead and is disconnected. The tracker
* thread receives ACKs and NACKs from its clients, which other threads can access. It gets information
* about client throughput rates from the controller thread and uses this information to construct trees to
* optimally distribute the stream.
**/

#ifndef TRACKER_H
#define TRACKER_H

#include "../common/socket_functions.h"
#include "../common/os_functions.h"
#include "../common/tcpstack.h"
#include "../common/data.h"

#include "controller.h"

#include <boost/thread/mutex.hpp>
#include <map>

//Number of slices the stream should be divided into. Defines the number of trees that will be constructed
//Larger number will result in administrative overhead
const int k_slices=50;

struct STreeNode;

/**
* Structure to save information about clients
**/
struct SClientData
{
	SClientData(void){ ip=0; port=0; rtt=0.f;}

	SOCKET s;
	unsigned int lastpingtime;
	unsigned int lastpong;
	CTCPStack tcpstack;

	unsigned int ip;
	unsigned int port;

	std::vector<STreeNode*> treenodes;

	float rtt;
};

/**
* Structure to save information about how a message is spread in a tree
**/
struct SSpread
{
	unsigned int id;
	bool forward;
	unsigned int load;
	bool child;
};

/**
* A node in a tree
**/
struct STreeNode
{
	STreeNode(void){ ref=0; parent=NULL; spread_ref=false; root_latency=0.5f;}
	std::vector<STreeNode*> children;
	std::vector<STreeNode*> ref_nodes;
	STreeNode *parent;
	SBest *data;
	int ref;
	int k;
	bool spread_ref;
	float root_latency;
};

/**
* Structure to save information about new clients
**/
struct SNewClient
{
	unsigned int ip;
	unsigned short port;
	unsigned int bandwidth;
	SOCKET s;
};

/**
* Structure to save information about acks
**/
struct SAck
{
	unsigned int msgid;
	unsigned int sourceip;
	unsigned short sourceport;
	float rtt;
};

/**
* Currently unused
**/
struct SResend
{
	unsigned int msgid;
	unsigned int ip;
	unsigned short port;
};

class Controller;
class Input;

/**
* Tracker thread class
**/
class Tracker
{
public:
	/**
	* Initialize tracker by setting the port it should listen on and the bandwidth it can use for the trees
	**/
	Tracker(unsigned short pPort, unsigned int exploit_bandwidth);

	/**
	* Functions for connecting this thread with the other two threads
	**/ 
	void setController(Controller *pController);
	void setInput(Input *pInput);

	/**
	* Get spread information about a data slice k
	**/
	std::vector<SSpread> getSpreadNodes(int k);

	/**
	* Get new clients
	**/
	std::vector<SNewClient> getNewClients(void);
	/**
	* Get newly disconnected clients
	**/
	std::vector<SNewClient> getExitClients(void);
	/**
	* Get new acks
	**/
	std::vector<SAck> getNewAcks(void);
	/**
	* Get new resends
	**/
	std::vector<SResend> getNewResends(void);

	/**
	* Main thread function
	**/
	void operator()(void);

	/**
	* Update the trees, to accomodate new and changed clients
	**/
	void update_spreads(void);

	/**
	* Visualize the trees
	**/
	void visualize_trees(void);

private:

	/**
	* Get spread information starting with tree node 'curr'
	**/
	std::vector<SSpread> getSpreadNodes(STreeNode *curr);

	/**
	* remove a client from the tracker specified by socket 's'
	**/
	void removeClient(SOCKET s);
	/**
	* handle a new packet with data 'data' from client with clientdata 'cd'
	**/
	void receivePacket( SClientData *cd, CRData &data);
	/**
	* Optimize the tree starting with 'root' as root node
	**/
	void optimizeTree(STreeNode *root);
	/**
	* Optimize the tree by allowing nodes to adopt other nodes
	**/
	void modifyTree(STreeNode *root);
	/**
	* If possible remove children from the root and add them somewhere else
	**/
	bool makeRootFree(STreeNode *root);
	/**
	* Enforce the current client bandwidths in the tree with root node 'root'. curr denotes the current
	* Node that is being processed.
	**/
	void enforceConstraints(STreeNode *root, STreeNode *curr=NULL);
	/**
	* Add a new nodes to every tree using data 'nn' and 'cd'
	**/
	bool addNewNode(SBest *nn, SClientData *cd);
	/**
	* Add a new node to the tree specified by its root 'root'
	**/
	bool addNewNodeSlice(STreeNode* root, SBest *nn, SClientData *cd);
	/**
	* Add existing node 'curr' to the tree specified by root 'root'.
	* 'curr' can have children.
	**/
	bool addExistingNode(STreeNode* root, STreeNode *curr);
	/**
	* Detach a node from its parent
	**/
	void detachChild(STreeNode *child);
	/**
	* Attach node 'new_child' to 'parent'. Afterwards 'parent' is 'new_child''s parent
	**/
	void attachChild(STreeNode *parent, STreeNode *new_child);
	/**
	* Do the tree optimizations and send the modifications to the clients
	**/
	void updateSpread(void);
	/**
	* Send to the node 'curr' which children it has in that tree - to whom it has to relay
	* messages if they're in this tree
	**/
	void sendSpread(STreeNode *curr);
	/**
	* Get a list of all tree nodes in the tree defined by 'root'
	**/
	std::vector<STreeNode*> getTreeNodes(STreeNode *root);
	/**
	* Exchange the nodes n1 and n2. That means n2 has then n1's parent as parent and
	* vise versa, as well as n2 has n1's children.
	**/
	void switchNodes(STreeNode *n1, STreeNode *n2);
	/**
	* Return a measure of the performance tree with root 'root' has. Smaller is better
	**/
	float evaluateTreePerformance(STreeNode *root);
	/**
	* Draw the trees
	**/
	void drawTrees(void);

	/**
	* Draw the tree
	**/
	std::string drawTree(STreeNode *n);

	//The TCP socket the tracker listens on
	SOCKET server_socket;
	//The port the tracker listen on
	unsigned short port;

	//List of all clients
	std::vector<SOCKET> clients;
	//Structure to save client data
	std::map<SOCKET, SClientData> client_data;

	//Last time the trees were updated
	unsigned int last_spread_update;

	//Available exploration bandwidth in byte/s
	unsigned int t_exploit_bandwidth;

	//Pointers to other threads
	Controller *controller;
	Input *input;

	//List of nodes which need new information about their children
	std::map<STreeNode*, bool> opt_update;
	//The roots of the trees
	std::vector<STreeNode*> roots;
	//Information about the nodes
	std::map<unsigned int, SBest*> nodes_info;
	//Nodes that could not be added to a tree and wait for assignment
	std::vector<STreeNode*> unasignable_nodes;
	//Mutex to synchronize acesses to the tree
	boost::mutex tree_mutex;

	//Class for tcp message encapsulation
	CTCPStack stack;

	bool viz_trees;

	//List of new clients
	std::vector<SNewClient> new_clients;
	//List of clients that exited
	std::vector<SNewClient> exit_clients;
	//List of received acks
	std::vector<SAck> new_acks;
	//Currently unused
	std::vector<SResend> new_resends;
	//Mutex to synchronize acesses to above strucutres
	boost::mutex mutex;
};

#endif //TRACKER_H