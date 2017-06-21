/**
* Class to packetize tcp streams. 
**/

#ifndef TCPSTACK_H
#define TCPSTACK_H

#include <vector>
#include "socket_functions.h"
#include "data.h"

#define MAX_PACKETSIZE	unsigned int

class CTCPStack
{
public:
	void AddData(char* buf, size_t datasize);

	char* getPacket(size_t* packsize);

	size_t Send(SOCKET s, char* buf, size_t msglen);
	size_t Send(SOCKET s, CWData &data);
	size_t Send(SOCKET s, const std::string &msg);

    void reset(void);

	char *getBuffer();
	size_t getBuffersize();

private:
	
	std::vector<char> buffer;
};

#endif //TCPSTACK_H