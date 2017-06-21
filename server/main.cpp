/**
* Main file. Parse the parameters and start all the threads.
**/
#ifdef _WIN32
#include <conio.h>
#include <Windows.h>
#endif

#include <iostream>
#include "controller.h"

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>

#include "../common/settings.h"


int main(int argc, char *argv[])
{
	if(argc<3)
	{
		std::cout << "Start with qstream [target_server url] [bandwidth in bytes/s]" << std::endl;
		return 0;
	}
	// Start input, tracker and controller thread and connect them to each other
	Input *input=new Input(argv[1]);
	boost::thread input_thread(boost::ref(*input));
	input_thread.yield();

	//90% of available bandwidth for exploitation
	Tracker *tracker=new Tracker(tracker_port, (unsigned int)((float)atoi(argv[2])*0.9f+0.5f));
	Controller *controller=new Controller(input, tracker, (unsigned int)atoi(argv[2]) );
	tracker->setController(controller);
	tracker->setInput(input);

	boost::thread tracker_thread(boost::ref(*tracker));
	tracker_thread.yield();

#ifdef _WIN32
	boost::thread controller_thread(boost::ref(*controller));
	controller_thread.yield();

	while(true)
	{
		Sleep(10);
		if( _kbhit()!=0 )
		{
			tracker->visualize_trees();
		}
	}
#else
	//Start the controller thread in the main thread
	(*controller)();
#endif
}