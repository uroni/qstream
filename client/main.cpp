/**
* Process entry point. Parse program parameters.
* Setup and start the threads with these parameters
**/

#ifdef _WIN32
#include <windows.h>
#endif
#include "trackerconnector.h"
#include "output.h"
#include "controller.h"
#include "../common/settings.h"
#include "../common/os_functions.h"
#include "../common/log.h"
#include <iostream>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>

const int num_clients=1;

int main(int argc, char* argv[])
{
	if(argc<3)
	{
		std::cout << "start with qstream_client [tracker] [bandwidth] ([output port] [controller port])" << std::endl;
		return 1;
	}
	unsigned short out_port=output_port;
	if(argc>3)
	{
		out_port=(unsigned short)atoi(argv[3]);
	}
	unsigned short controller_port=client_controller_port;
	if(argc>4)
	{
		controller_port=(unsigned short)atoi(argv[4]);
	}

	//os_sleep(5000);

	for(int i=0;i<num_clients;++i)
	{
		TrackerConnector *tracker_conn=new TrackerConnector(argv[1], tracker_port, controller_port+i,(unsigned int)atoi(argv[2]));
		Output *output=new Output(out_port+i);
		Controller *controller=new Controller(controller_port+i, tracker_conn, (unsigned int)atoi(argv[2]), output);
		output->setController(controller);


		boost::thread output_thread(boost::ref(*output));
		output_thread.yield();

		boost::thread controller_thread(boost::ref(*controller));
		controller_thread.yield();

		if(i+1>=num_clients)
		{
			while(true)
			{
				(*tracker_conn)();
				os_sleep(1000);
				log("Reconnecting to stracker");
			}
		}
		else
		{
			boost::thread tracker_thread(boost::ref(*tracker_conn));
			tracker_thread.yield();
		}
	}

	return 0;
}
