#ifndef MEMPIPE_H_
#define MEMPIPE_H_

#include "Pipe.h"
#include <deque>
#include <string>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

class CMemPipe : public IPipe
{
public:
	CMemPipe(void);

	virtual void setBlocking(bool b);

	virtual size_t Read(char *buffer, size_t bsize);
	virtual bool Write(const char *buffer, size_t bsize);
	virtual bool Read(std::string *str);
	virtual bool Write(const std::string &str);
	
	virtual bool Read(CRData &dta);
	virtual bool Write(CWData &dta);

	virtual bool isWritable(void);
	virtual bool isReadable(void);
	virtual bool isReadable(unsigned int wait_for_ms);
	
	virtual bool isWaiting(void);

	virtual int readCount();
	virtual int grabCount();
	virtual void grab();
	virtual void drop(bool notify);
	virtual void wakeup();
	
private:

	std::deque<std::string> queue;
	
	boost::mutex mutex;
	boost::condition cond;
	bool blocking;
	bool refcountchange;

	int refcount;
	
	int readcount;
	
	bool waiting;
};

#endif /*MEMPIPE_H_*/
