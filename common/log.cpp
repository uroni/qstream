#include <iostream>
#include "log.h"

#include <boost/thread/mutex.hpp>

boost::mutex log_mutex;

void log(std::string str)
{
	boost::mutex::scoped_lock lock(log_mutex);
	std::cout << str << std::endl;
}