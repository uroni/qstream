#ifndef TYPES_H_
#define TYPES_H_

typedef unsigned char uchar;

#ifndef _WIN32
typedef int SOCKET;
#else
typedef _w64 unsigned int SOCKET;
#endif

typedef unsigned int TYPE_IP;
typedef unsigned short TYPE_PORT;
typedef unsigned int TYPE_CONNID;

const unsigned int MR_MAX_UINT=4294967294UL;

#define E_ERROR -1

#endif /*TYPES_H_*/
