#include "os_functions.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <boost/thread/xtime.hpp>

unsigned int os_gettimems(void)
{
#ifdef _WIN32
	return GetTickCount();
#else
	boost::xtime xt;
    boost::xtime_get(&xt, boost::TIME_UTC);
	static boost::int_fast64_t start_t=xt.sec;
	xt.sec-=start_t;
	unsigned int t=xt.sec*1000+(unsigned int)((double)xt.nsec/1000000.0);
	return t;
#endif
}

void os_sleep(unsigned int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep(ms*1000);
#endif
}


