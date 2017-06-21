#include "msg_data.h"
#include "packet_ids.h"

/**
* Parse an exploration packet
**/
msg_data::msg_data(CRData &data)
{
	err=false;
	if(!data.getUInt(&msgid) )
	{
		err=true;
		return;
	}
	unsigned char nhops;
	if(!data.getUChar(&nhops))
	{
		err=true;
		return;
	}
	for(unsigned char i=0;i<nhops;++i)
	{
		unsigned int hop_ip;
		unsigned short hop_port;
		if(!data.getUInt(&hop_ip) )
		{
			err=true;
			return;
		}
		if(!data.getUShort(&hop_port) )
		{
			err=true;
			return;
		}
		hops.push_back(std::pair<unsigned int, unsigned short>(hop_ip, hop_port) );
	}
	if(!data.getUChar(&curr_hop))
	{
		err=true;
		return;
	}
	if(curr_hop>nhops)
	{
		err=true;
		return;
	}
	if(!data.getUShort(&buf_size) )
	{
		err=true;
		return;
	}
	if(data.getLeft()<buf_size)
	{
		err=true;
		return;
	}
	buf=data.getCurrDataPtr();
}

/**
* Returns if there was an error parsing the packet
**/
bool msg_data::hasError(void)
{
	return err;
}

/**
* Construct an exploration packet with id 'pMsgid'. Hops 'pHops' consisting of pairs of ip and port. And payload data 'pBuf' with
* size 'pBuf_size'
**/
msg_data::msg_data(unsigned int pMsgid,const std::vector<std::pair<unsigned int, unsigned short> > pHops, const char* pBuf, size_t pBuf_size)
{
	hops=pHops;
	curr_hop=0;
	buf=pBuf;
	buf_size=pBuf_size;
	msgid=pMsgid;
}

/**
* Get the next hop of this packet. Returns 0,0 if this is the last hop
**/
std::pair<unsigned int, unsigned short> msg_data::getNextHop(void)
{
	if(curr_hop<hops.size())
	{
		return hops[curr_hop];
	}
	else
		return std::pair<unsigned int, unsigned short>(0,0);
}

/**
* Return the ultimate target of this packet
**/
std::pair<unsigned int, unsigned short> msg_data::getTarget(void)
{
	if(!hops.empty())
	{
		return hops[hops.size()-1];
	}
	else
	{
		return std::pair<unsigned int, unsigned short>(0,0);
	}
}

/**
* Increment the current hop
**/
void msg_data::incrementHop(void)
{
	if((size_t)curr_hop+1<=hops.size())
		++curr_hop;
}

/**
* Return the data saved in this message
**/
const char *msg_data::getBuf(void)
{
	return buf;
}

/**
* Return the size of the data saved in this message
**/
unsigned short msg_data::getBuf_size(void)
{
	return buf_size;
}

/**
* Return the id of this pacekt
**/
unsigned int msg_data::getMsgID(void)
{
	return msgid;
}

/**
* Construct the message
**/
void msg_data::getMessage(CWData &data)
{
	data.addUChar(CC_DATA);
	data.addUInt(msgid);
	data.addUChar((unsigned char)hops.size());
	for(size_t i=0;i<hops.size();++i)
	{
		data.addUInt(hops[i].first);
		data.addUShort(hops[i].second);
	}
	data.addUChar(curr_hop);
	data.addUShort(buf_size);
	data.addBuffer(buf, buf_size);
}