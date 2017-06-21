/**
* Class to construct and parse a message that is send through the tree.
* The message is constructed with a certain id and data it has to
* carry.
**/

#include "data.h"

class msg_spread
{
public:
	msg_spread(CRData &data);
	msg_spread(unsigned int pMsgid, const char* pBuf, size_t pBuf_size);

	void getMessage(CWData &data);
	const char *getBuf(void);
	unsigned short getBuf_size(void);

	unsigned int getMsgID(void);

	bool hasError(void);

private:

	const char *buf;
	unsigned short buf_size;
	unsigned int msgid;

	bool err;
};