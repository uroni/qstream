#include <memory.h>
#include <boost/thread/xtime.hpp>

#include "MemPipe.h"
#include "data.h"

CMemPipe::CMemPipe(void)
{
	blocking=true;
	refcount=0;
	refcountchange=false;
	readcount=0;
	waiting=false;
}

void CMemPipe::setBlocking(bool b)
{
	boost::mutex::scoped_lock lock(mutex);
	blocking=b;
}

size_t CMemPipe::Read(char *buffer, size_t bsize)
{
	boost::mutex::scoped_lock lock(mutex);
	
	if( blocking==true )
	{
		if( queue.size()==0 )
		{
			++readcount;
			cond.wait(lock);
			--readcount;
		}

		if( queue.size()==0 )
			return false;
	}
	else
	{
		if( queue.size()==0 )
		{
			return false;
		}
	}
	
	std::string *cstr=&queue[0];
	
	size_t psize=cstr->size();
	
	if( psize<=bsize )
	{
		memcpy( buffer, cstr->c_str(), psize );
		queue.erase( queue.begin() );
		return psize;
	}
	else
	{
		memcpy( buffer, cstr->c_str(), bsize );
		cstr->erase(0, bsize );
		return bsize;
	}
}

bool CMemPipe::Write(const char *buffer, size_t bsize)
{
	boost::mutex::scoped_lock lock(mutex);
	
	queue.push_back("");
	std::deque<std::string>::iterator iter=queue.end();
	--iter;
	std::string *nstr=&(*iter);
	
	nstr->resize( bsize );
	memcpy( (char*)nstr->c_str(), buffer, bsize );
	
	if( blocking==true )
	{
		cond.notify_one();
	}
	
	return true;
}

bool CMemPipe::Read(std::string *str)
{
	boost::mutex::scoped_lock lock(mutex);
	
	if(blocking==true )
	{
		if(queue.size()==0 )
		{
			++readcount;
			cond.wait(lock);
			--readcount;
		}

		if( queue.size()==0 )
			return false;
	}
	else
	{
		if( queue.size()==0 )
			return false;
	}
	
	std::string *fs=&queue[0];
	
	size_t fsize=fs->size();
	
	str->resize( fsize );
	 
	memcpy( (char*) str->c_str(), fs->c_str(), fsize );
	
	queue.erase( queue.begin() );
	
	return true;		
}

bool CMemPipe::Write(const std::string &str)
{
	boost::mutex::scoped_lock lock(mutex);
	
	queue.push_back( str );
	
	if( blocking==true )
	{
		cond.notify_one();
	}
	
	return true;
}

bool CMemPipe::isWritable(void)
{
	return true;
}

bool CMemPipe::isReadable(void)
{
	boost::mutex::scoped_lock lock(mutex);
	if( queue.size()>0 )
		return true;
	else
		return false;
}

bool CMemPipe::isReadable(unsigned int wait_for_ms)
{
	boost::mutex::scoped_lock lock(mutex);

	if( queue.size()>0 )
		return true;
	
	boost::xtime xt;
    	xtime_get(&xt, boost::TIME_UTC);
	xt.nsec+=wait_for_ms*1000000;
	waiting=true;
	cond.timed_wait( lock, xt );
	waiting=false;

	if( queue.size()>0 )
		return true;
	else
		return false;
}

bool CMemPipe::Read(CRData &dta)
{
	boost::mutex::scoped_lock lock(mutex);
	
	if( blocking==true )
	{
		while( queue.empty() )
		{
			++readcount;
			cond.wait(lock);
			--readcount;
			if(refcountchange && queue.empty() )
			{
				refcountchange=false;
				return false;
			}
		}
	}
	else
	{
		if( queue.size()==0 )
		{
			return false;
		}
	}
	
	std::string *cstr=&queue[0];
	
	size_t psize=cstr->size();
	
	dta.set(cstr->c_str(), psize, true );
	
	queue.erase( queue.begin() );
	
	return true;
}

bool CMemPipe::Write(CWData &dta)
{
	return Write( dta.getDataPtr(), dta.getDataSize() );
}

int CMemPipe::grabCount()
{
	boost::mutex::scoped_lock lock(mutex);
	return refcount;
}

void CMemPipe::grab()
{
	boost::mutex::scoped_lock lock(mutex);
	++refcount;
}

void CMemPipe::drop(bool notify)
{
	boost::mutex::scoped_lock lock(mutex);
	--refcount;

	if( refcount==0 )
	{
		lock.unlock();
		delete this;
	}
	else if( notify==true )
	{
		if( blocking==true )
		{
			cond.notify_all();
		}
		refcountchange=true;
	}
}

int CMemPipe::readCount()
{
    boost::mutex::scoped_lock lock(mutex);
    return readcount;
}

bool CMemPipe::isWaiting()
{
    boost::mutex::scoped_lock lock(mutex);
    return waiting;
}

void CMemPipe::wakeup()
{
    cond.notify_all();
}
