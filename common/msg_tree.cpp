#include "msg_tree.h"
#include "packet_ids.h"

/**
* Parse a tree message
**/
msg_tree::msg_tree(CRData &data)
{
	err=false;

	if(!data.getInt(&k))
	{
		err=true;
		return;
	}

	if(!data.getInt(&slices))
	{
		err=true;
		return;
	}

	unsigned int r_nodes;
	if(!data.getUInt(&r_nodes))
	{
		err=true;
		return;
	}

	for(unsigned int i=0;i<r_nodes;++i)
	{
		unsigned int ip;
		if(!data.getUInt(&ip))
		{
			err=true;
			return;
		}
		unsigned short port;
		if(!data.getUShort(&port))
		{
			err=true;
			return;
		}
		relay_nodes.push_back(std::pair<unsigned int, unsigned short>(ip,port));
	}
}

/**
* Construct a tree message. The client this message is sent to has the children in 'pRelay_nodes'.
* 'pRelay_nodes' is a list of pairs consisting of ip and port. 'pK' says which tree corresponds to
* these children. 'pSlices' says how many trees there are.
**/
msg_tree::msg_tree(std::vector<std::pair<unsigned int,unsigned short> > pRelay_nodes, int pK, int pSlices)
{
	relay_nodes=pRelay_nodes;
	k=pK;
	slices=pSlices;
}

/**
* Get the children. (List of ip,port pairs
**/
std::vector<std::pair<unsigned int,unsigned short> > msg_tree::getRelayNodes(void)
{
	return relay_nodes;
}

/**
* Construct the message
**/
void msg_tree::getMessage(CWData &data)
{
	data.addUChar(TRACKER_TREE);
	data.addInt(k);
	data.addInt(slices);
	data.addUInt(relay_nodes.size());
	for(size_t i=0;i<relay_nodes.size();++i)
	{
		data.addUInt(relay_nodes[i].first);
		data.addUInt(relay_nodes[i].second);
	}
}

/**
* Returns if there was a parsing error
**/
bool msg_tree::hasError(void)
{
	return err;
}

/**
* Get the tree number
**/
int msg_tree::getK(void)
{
	return k;
}

/**
* Get the total number of trees
**/
int msg_tree::getSlices(void)
{
	return slices;
}

