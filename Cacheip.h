//#include "Header.h"
#ifndef _CACHEIP_H_
#define _CACHEIP_H_

struct cache_ip{
    char * IP;
    char * hostname;
    struct cache_ip * next;
    short * h_length;
};

void add_to_ip_cache(struct hostent *remoteHost, char * hostname);
char * get_ip_from_hostname_cache(char * hostname);
short * get_hlength_from_hostname_cache(char * hostname);
void delete_cache();

struct cache_ip * head;

#endif

GET http://netsys.cs.colorado.edu/ HTTP/1.0

