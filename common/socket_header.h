#ifdef _WIN32
#	undef SOCKET_ERROR
#	include <winsock2.h>
#	include <windows.h>
#	define MSG_NOSIGNAL 0
#	define socklen_t int
#else
#	include <string>
#	include <signal.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#	include <unistd.h>
#	include <fcntl.h>
#	define SOCKET_ERROR -1
#	define closesocket close
typedef int SOCKET;
#	define Sleep(x) usleep(x*1000)
#endif
