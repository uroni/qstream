/**
* Class to parse and construct an acknowldegement, which will be send
* from a client to the tracker on receiving an exploration packet (and
* being the last client in the chain in this packet)
**/
#include "data.h"

class msg_ack
{
public:
	msg_ack(CRData &data);
	msg_ack(unsigned int pMsg_id, unsigned int pSource_ip, unsigned short pSource_port);

	void getMessage(CWData &data);

	unsigned int getMsgID(void);
	unsigned int getSourceIP(void);
	unsigned short getSourcePort(void);

	bool hasError(void);

private:

	unsigned int msg_id;
	unsigned int source_ip;
	unsigned short source_port;

	bool err;
};