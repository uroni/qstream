#include <memory.h>
#include "socket_header.h"
#include "socket_functions.h"
#include "log.h"

bool setSockP(SOCKET sock)
{
#ifdef _WIN32
		DWORD dwBytesReturned = 0;
		BOOL bNewBehavior = FALSE;
		int status;

		// disable  new behavior using
		// IOCTL: SIO_UDP_CONNRESET
		#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
		status = WSAIoctl(sock, SIO_UDP_CONNRESET,
						&bNewBehavior, sizeof(bNewBehavior),
					NULL, 0, &dwBytesReturned,
					NULL, NULL);
		if (SOCKET_ERROR == status)
		{
			return false;
		}
#endif
        return true;
}

SOCKET os_createSocket(bool pUDP)
{
#ifdef _WIN32
	WSADATA wsadata;
	int rc = WSAStartup(MAKEWORD(2,0), &wsadata);
	if(rc == SOCKET_ERROR)	return 0;
#endif


	SOCKET s;
	if(pUDP)
	{
		s= socket(AF_INET, SOCK_DGRAM, 0);	
		setSockP(s);
	}
	else
	{
		s=socket(AF_INET, SOCK_STREAM, 0);
	}
	return s;
}

int os_sendto(SOCKET s, unsigned int ip, unsigned short port, const char *buffer, unsigned int bsize)
{
	sockaddr_in addr;
	addr.sin_addr.s_addr=ip;
	addr.sin_port=htons( port );
	addr.sin_family=AF_INET;
	
	return sendto(s, buffer, bsize, MSG_NOSIGNAL, (sockaddr*)&addr, sizeof(sockaddr_in) );
}

int os_recvfrom(SOCKET s, char *buffer, unsigned int bsize, unsigned int &fromip, unsigned short &fromport)
{
	socklen_t sockaddrlen=sizeof(sockaddr_in);
	sockaddr_in addr;
	int rc=recvfrom(s, buffer, bsize, 0, (sockaddr*)&addr, &sockaddrlen);
	fromip=addr.sin_addr.s_addr;
	fromport=ntohs(addr.sin_port);
	return rc;	 
}

bool os_bind(SOCKET s, unsigned short port)
{
	sockaddr_in addr;
	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	addr.sin_addr.s_addr=INADDR_ANY;
	int rc=bind(s,(sockaddr*)&addr,sizeof(addr));
	if(rc==-1)
	{
		return false;
	}
	else
		return true;
}

unsigned short os_getsocketport(SOCKET s)
{
	sockaddr_in addr;
	socklen_t len=sizeof(sockaddr_in);
	int rc=getsockname(s, (sockaddr*)&addr, &len );
	if( rc==0 )
	    return ntohs(addr.sin_port);
	else
	    return 0;
}

unsigned int os_getlocalhost(void)
{
	return 0x0100007f; //TODO: FIX FOR BIG-ENDIAN
}

std::vector<SOCKET> os_select(const std::vector<SOCKET> &s, unsigned int timeoutms)
{
	fd_set fdset;
	FD_ZERO(&fdset);
	SOCKET max=0;
	for(size_t i=0;i<s.size();++i)
	{
		if(max==0 || s[i]>max)
			max=s[i];
		FD_SET(s[i], &fdset);
	}
	timeval lon;
	lon.tv_sec=timeoutms/1000;
	lon.tv_usec=timeoutms%1000*1000;
	int rc = select(max+1, &fdset, 0, 0, &lon);

	std::vector<SOCKET> ret;
	if(rc>0)
	{
		for(size_t i=0;i<s.size();++i)
		{
			if(FD_ISSET(s[i], &fdset) )
			{
				ret.push_back(s[i]);
			}
		}
	}
	return ret;
}

bool os_listen(SOCKET s, int count)
{
	listen(s, count);
	return true;
}

SOCKET os_accept(SOCKET s, unsigned int *ip)
{
	sockaddr_in addr;
	socklen_t addrlen=sizeof(sockaddr_in);
	SOCKET ns=accept(s, (sockaddr*)&addr, &addrlen);
	if(ip!=NULL)
		*ip=addr.sin_addr.s_addr;
	return ns;
}

int os_recv(SOCKET s, char *buffer, size_t blen)
{
	return recv(s, buffer, blen, MSG_NOSIGNAL);
}

int os_send(SOCKET s, const char *buffer, size_t blen)
{
	return send(s, buffer, blen, MSG_NOSIGNAL);
}

in_addr getIP(std::string ip)
{
	const char* host=ip.c_str();
	in_addr dest;
	unsigned int addr = inet_addr(host);
	if (addr != INADDR_NONE)
	{
		dest.s_addr = addr;
		return dest;
	}
	else
	{
		hostent* hp = gethostbyname(host);
		if (hp != 0)
		{
			memcpy(&(dest), hp->h_addr, hp->h_length);
		}
		else
		{
			memset(&dest,0,sizeof(in_addr) );
			return dest;
		}
	}
	return dest;
}


unsigned int os_resolv(std::string name)
{
	return getIP(name).s_addr;
}

bool os_connect(SOCKET s, unsigned int server_ip, unsigned short server_port)
{
	sockaddr_in addr;
	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(server_port);
	addr.sin_addr.s_addr=server_ip;
	int rc=connect(s, (sockaddr*)&addr, sizeof(sockaddr_in));
	return rc>=0;
}

void os_nagle(SOCKET s, bool b)
{
#ifdef _WIN32
	BOOL opt=b?FALSE:TRUE;
	int err=setsockopt(s,IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(BOOL) );
	if( err==SOCKET_ERROR )
		log("Error: Setting TCP_NODELAY failed");
#else
	int opt=b?1:0;
	int err=setsockopt(s, IPPROTO_TCP, TCP_CORK, (char*)&opt, sizeof(int) );
	if( err==SOCKET_ERROR )
	{
		log("Error: Setting TCP_CORK failed.");
	}
#endif
}

bool os_set_recv_window(SOCKET s, unsigned int size)
{
#ifdef _WIN32
	if(setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&size, sizeof(unsigned int))!=0)
		return false;
#endif
	return true;
}

bool os_set_send_window(SOCKET s, unsigned int size)
{
	if(setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(unsigned int))!=0)
		return false;
	return true;
}

void os_closesocket(SOCKET s)
{
	closesocket(s);
}