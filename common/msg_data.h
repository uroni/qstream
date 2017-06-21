/**
* Class to parse and construct a exploration packet.
**/

#include "data.h"

class msg_data
{
public:
	/**
	* Parse an exploration packet
	**/
	msg_data(CRData &data);
	/**
	* Construct an exploration packet with id 'pMsgid'. Hops 'pHops' consisting of pairs of ip and port. And payload data 'pBuf' with
	* size 'pBuf_size'
	**/
	msg_data(unsigned int pMsgid, const std::vector<std::pair<unsigned int, unsigned short> > pHops, const char* pBuf, size_t pBuf_size);

	/**
	* Get the next hop of this packet. Returns 0,0 if this is the last hop
	**/
	std::pair<unsigned int, unsigned short> getNextHop(void);
	/**
	* Return the ultimate target of this packet
	**/
	std::pair<unsigned int, unsigned short> getTarget(void);
	/**
	* Increment the current hop
	**/
	void incrementHop(void);

	/**
	* Construct the message
	**/
	void getMessage(CWData &data);
	/**
	* Return the data saved in this message
	**/
	const char *getBuf(void);
	/**
	* Return the size of the data saved in this message
	**/
	unsigned short getBuf_size(void);
	/**
	* Return the id of this pacekt
	**/
	unsigned int getMsgID(void);

	/**
	* Returns if there was an error parsing the packet
	**/
	bool hasError(void);

private:
	std::vector<std::pair<unsigned int, unsigned short> > hops;
	unsigned char curr_hop;

	const char *buf;
	unsigned short buf_size;

	unsigned int msgid;

	bool err;
};