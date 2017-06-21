#ifndef PIPE_H_
#define PIPE_H_

#include <string>

class CWData;
class CRData;

class IPipe
{
public:
	virtual void setBlocking(bool b)=0; //default: true

	virtual size_t Read(char *buffer, size_t bsize)=0;
	virtual bool Write(const char *buffer, size_t bsize)=0;
	virtual bool Read(std::string *str)=0;
	virtual bool Write(const std::string &str)=0;
	
	virtual bool Read(CRData &dta)=0;
	virtual bool Write(CWData &dta)=0;	

	virtual bool isWritable(void)=0;
	virtual bool isReadable(void)=0;
	virtual bool isReadable(unsigned int wait_for_ms)=0;
	
	virtual int readCount(void)=0;
	virtual bool isWaiting(void)=0;

	virtual int grabCount()=0;
	virtual void grab()=0;
	virtual void drop(bool notify=false)=0;
	virtual void wakeup()=0;
};

#endif /*PIPE_H_*/