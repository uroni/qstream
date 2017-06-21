/**
* Class to contruct and parse the tree message. This message is used to inform a client about which
* children it has in a certain tree.
**/

#include "data.h"

class msg_tree
{
public:
	/**
	* Parse a tree message
	**/
	msg_tree(CRData &data);
	/**
	* Construct a tree message. The client this message is sent to has the children in 'pRelay_nodes'.
	* 'pRelay_nodes' is a list of pairs consisting of ip and port. 'pK' says which tree corresponds to
	* these children. 'pSlices' says how many trees there are.
	**/
	msg_tree(std::vector<std::pair<unsigned int,unsigned short> > pRelay_nodes, int pK, int pSlices);

	/**
	* Construct the message
	**/
	void getMessage(CWData &data);

	/**
	* Get the children. (List of ip,port pairs
	**/
	std::vector<std::pair<unsigned int,unsigned short> > getRelayNodes(void);
	/**
	* Get the tree number
	**/
	int getK(void);
	/**
	* Get the total number of trees
	**/
	int getSlices(void);

	/**
	* Returns if there was a parsing error
	**/
	bool hasError(void);

private:

	std::vector<std::pair<unsigned int,unsigned short> > relay_nodes;
	int k;
	int slices;

	bool err;
};