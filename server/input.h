/**
* Connects itself to the real streaming server and writes the data it
* receives into buffers which can be accessed from the other
* threads
**/
#ifndef INPUT_H
#define INPUT_H

#include <queue>
#include <map>
#include <boost/thread/mutex.hpp>

/**
* Structure to save a buffer received from the real streaming server
**/
struct SBuffer
{
	SBuffer(){ already_used=false; }
	char *data;
	size_t datasize;
	size_t id;
	unsigned int created;
	bool already_used;
};

/**
* The input thread
**/
class Input
{
public:
	/**
	* Initialize the input thread. Use the URL pURL for acessing the stream
	* via HTTP
	*/
	Input(const std::string &pURL);

	/**
	* main thread function
	**/
	void operator()(void);

	/**
	* Get newly received buffers
	**/
	std::vector<SBuffer*> getNewBuffers(void);
	/**
	* Get a specific (old) buffer with id 'id'
	**/
	SBuffer * getBuffer(size_t id);

	/**
	* Get the average amount of packets received per second
	**/
	float getPacketsPerSecond(void);
	/**
	* Get the maximal amount of packets received per second
	**/
	float getMaxPacketsPerSecond(void);

private:

	/**
	* Clean old buffers
	**/
	void cleanBuffer(void);

	//Structures for saving the buffers
	std::queue<SBuffer*> new_buffers;
	std::queue<SBuffer*> old_buffers;
	std::queue<SBuffer*> buffer_trash;

	//Structure to map buffer ids to buffers
	std::map<size_t, SBuffer*> buffer_ids;

	//The stream url
	std::string url;

	//Current buffer id
	size_t curr_buffer_id;

	//Values to caclculate the packets per second and max packets per seconds
	float packets_sec;
	unsigned int packets;
	unsigned int last_packetcounttime;
	float max_packets_sec;

	boost::mutex mutex;
};

#endif //INPUT_H
