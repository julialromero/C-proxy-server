//#include "Header.h"
#ifndef _CACHEIP_H_
#define _CACHEIP_H_

struct cache_ip{
    char * IP;
    char * hostname;
    struct cache_ip * next;
    short * h_length;
};

struct blacklisted{
    char * hostval; // IP or hostname
    struct blacklisted * next;
};

void add_to_ip_cache(struct hostent *remoteHost, char * hostname);
char * get_ip_from_hostname_cache(char * hostname);
short * get_hlength_from_hostname_cache(char * hostname);
void delete_cache();
void print_blacklisted();
void load_blacklist();
void delete_blacklisted();
int check_if_blacklisted(struct hostent * remoteHost);

struct cache_ip * head;
struct blacklisted * blacklisthead;

#endif