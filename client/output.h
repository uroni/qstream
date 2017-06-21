/**
* Output thread. Listens on a TCP socket and sends the
* video stream to all applications who connect to it.
**/

#include <map>
#include <vector>
#include <queue>
#include <boost/thread/mutex.hpp>
#include "../common/types.h"

class Controller;

/**
* Structure to save a buffer
**/
struct SBufferObject
{
	bool operator<(const SBufferObject &other)
	{
		return id<other.id;
	}
	unsigned int id;
	char *buf;
	size_t bsize;
	unsigned int atime;
	int refc;
	bool nack;
};

/**
* Structure to save a pointer to a ts(transport stream) packet
**/
struct TSPacket
{
	char *buf;
	size_t bsize;
	SBufferObject *ref;
};

/**
* output thread class
**/
class Output
{
public:
	/**
	* Initialize output thread. Listen on port 'pPort'
	**/
	Output(unsigned short pPort);

	/**
	* Main thread function
	**/
	void operator()(void);

	/**
	* Add a buffer that should be send to all connected video players
	**/
	void addBufferObject(SBufferObject *obj);

	/**
	* Set the pointer to the controller thread
	**/
	void setController(Controller *pController);

private:
	// Id of the buffer which should be send next
	unsigned int next_id;
	// Id of the last buffer sent
	unsigned int past_id;
	// If true send a nack if 'next_id' is not present in time
	bool nack_next;
	// Time at which the last next buffer was present
	unsigned int time_last;

	//Port the output thread is listeing on
	unsigned short port;

	//True if there are no skipped packets
	bool stream_ok;

	//Returns the size of an accumulation of ts packets
	size_t getSize(const std::vector<TSPacket> &tsp);


	//Synchonize accesses to the buffers
	boost::mutex mutex;
	//True if we're in the middle of a ts packet
	bool ints;
	//Number of bytes left in the current ts packet
	int tsleft;
	//Structure to save received buffers
	std::map<unsigned int,SBufferObject*> buffers;
	//Queue of tspackets to be send
	std::queue<std::vector<TSPacket> > tspackets;
	//List of client sockets (clients here are the video players)
	std::vector<SOCKET> clients;

	//Pointer to the controller thread
	Controller *controller;
};