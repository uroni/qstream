/**
* Wrapper functions for berkley sockets
**/

#ifndef SOCKET_FUNCTIONS_H_
#define SOCKET_FUNCTIONS_H_

#include "types.h"
#include <vector>
#include <string>

SOCKET os_createSocket(bool pUDP=true);
int os_sendto(SOCKET s, unsigned int ip, unsigned short port, const char *buffer, unsigned int bsize);
int os_recvfrom(SOCKET s, char *buffer, unsigned int bsize, unsigned int &fromip, unsigned short &fromport);
bool os_bind(SOCKET s, unsigned short port);
unsigned short os_getsocketport(SOCKET s);
unsigned int os_getlocalhost(void);
std::vector<SOCKET> os_select(const std::vector<SOCKET> &s, unsigned int timeoutms);
SOCKET os_accept(SOCKET s, unsigned int *ip=NULL);
bool os_listen(SOCKET s, int count);
int os_recv(SOCKET s, char *buffer, size_t blen);
int os_send(SOCKET s, const char *buffer, size_t blen);
unsigned int os_resolv(std::string name);
bool os_connect(SOCKET s, unsigned int server_ip, unsigned short server_port);
void os_nagle(SOCKET s, bool b);
bool os_set_recv_window(SOCKET s, unsigned int size);
bool os_set_send_window(SOCKET s, unsigned int size);
void os_closesocket(SOCKET s);

#ifndef SOCKET_ERROR
#	define SOCKET_ERROR -1
#endif

#endif /*SOCKET_FUNCTIONS_H_*/

