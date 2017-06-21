/**
* Class to construct and parse a message that is send through the tree.
* The message is constructed with a certain id and data it has to
* carry.
**/

#include "msg_spread.h"
#include "packet_ids.h"

msg_spread::msg_spread(CRData &data)
{
	err=false;
	if(!data.getUInt(&msgid) )
	{
		err=true;
		return;
	}
	if(!data.getUShort(&buf_size))
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

msg_spread::msg_spread(unsigned int pMsgid, const char* pBuf, size_t pBuf_size)
{
	buf=pBuf;
	buf_size=pBuf_size;
	msgid=pMsgid;
}

const char *msg_spread::getBuf(void)
{
	return buf;
}

unsigned short msg_spread::getBuf_size(void)
{
	return buf_size;
}

void msg_spread::getMessage(CWData &data)
{
	data.addUChar(CC_SPREAD);
	data.addUInt(msgid);
	data.addUShort(buf_size);
	data.addBuffer(buf, buf_size);
}

bool msg_spread::hasError(void)
{
	return err;
}

unsigned int msg_spread::getMsgID(void)
{
	return msgid;
}