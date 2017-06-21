#include "tcpstack.h"
#include "data.h"
#include <memory.h>

#define SEND_TIMEOUT 10000
#define MAX_PACKET 4096

void CTCPStack::AddData(char* buf, size_t datasize)
{
	if(datasize>0)
	{
		size_t osize=buffer.size();
		buffer.resize(osize+datasize);
		memcpy(&buffer[osize], buf, datasize);
	}
}

size_t CTCPStack::Send(SOCKET s, char* buf, size_t msglen)
{
	char* buffer=new char[msglen+sizeof(MAX_PACKETSIZE)];

	MAX_PACKETSIZE len=(MAX_PACKETSIZE)msglen;

	memcpy(buffer, &len, sizeof(MAX_PACKETSIZE) );
	memcpy(&buffer[sizeof(MAX_PACKETSIZE)], buf, msglen);

	size_t currpos=0;

	while(currpos<msglen+sizeof(MAX_PACKETSIZE))
	{
		size_t ts=(std::min)((size_t)MAX_PACKET, msglen+sizeof(MAX_PACKETSIZE)-currpos);
		int rc=os_send(s, &buffer[currpos], ts);
		currpos+=ts;
		if(rc<0)
		{
			delete[] buffer;
			return 0;
		}
	}
	delete[] buffer;

	return msglen;
}

size_t  CTCPStack::Send(SOCKET s, CWData &data)
{
	return Send(s, data.getDataPtr(), data.getDataSize() );
}

size_t CTCPStack::Send(SOCKET s, const std::string &msg)
{
	return Send(s, (char*)msg.c_str(), msg.size());
}


char* CTCPStack::getPacket(size_t* packetsize)
{
	if(buffer.size()>1)
	{
		MAX_PACKETSIZE len;
		memcpy(&len, &buffer[0], sizeof(MAX_PACKETSIZE) );

		if(buffer.size()>=(size_t)len+sizeof(MAX_PACKETSIZE))
		{
			char* buf=new char[len+1];
			memcpy(buf, &buffer[sizeof(MAX_PACKETSIZE)], len);

			(*packetsize)=len;

			buffer.erase(buffer.begin(), buffer.begin()+len+sizeof(MAX_PACKETSIZE));

            buf[len]=0;

			return buf;
		}
	}
	return NULL;
}

void CTCPStack::reset(void)
{
        buffer.clear();
}

char *CTCPStack::getBuffer()
{
	return &buffer[0];
}

size_t CTCPStack::getBuffersize()
{
	return buffer.size();
}