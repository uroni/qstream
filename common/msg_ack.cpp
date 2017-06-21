/**
* Class to parse and construct an acknowldegement, which will be send
* from a client to the tracker on receiving an exploration packet (and
* being the last client in the chain in this packet)
**/
#include "msg_ack.h"
#include "packet_ids.h"

msg_ack::msg_ack(CRData &data)
{
	err=false;
	if(!data.getUInt(&msg_id))
	{
		err=true;
		return;
	}
	if(!data.getUInt(&source_ip))
	{
		err=true;
		return;
	}
	if(!data.getUShort(&source_port))
	{
		err=true;
		return;
	}
}

msg_ack::msg_ack(unsigned int pMsg_id, unsigned int pSource_ip, unsigned short pSource_port)
{
	msg_id=pMsg_id;
	source_ip=pSource_ip;
	source_port=pSource_port;
}

void msg_ack::getMessage(CWData &data)
{
	data.addUChar(TRACKER_ACK);
	data.addUInt(msg_id);
	data.addUInt(source_ip);
	data.addUShort(source_port);
}

unsigned int msg_ack::getMsgID(void)
{
	return msg_id;
}

unsigned int msg_ack::getSourceIP(void)
{
	return source_ip;
}

unsigned short msg_ack::getSourcePort(void)
{
	return source_port;
}

bool msg_ack::hasError(void)
{
	return err;
}
