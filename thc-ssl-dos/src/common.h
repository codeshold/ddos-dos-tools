
#ifndef __THC_COMMON_H__
#define __THC_COMMON_H__ 1

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#define FD_SETSIZE	1024

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

//#define int_ntoa(x)   inet_ntoa(*((struct in_addr *)&(x)))

#endif
