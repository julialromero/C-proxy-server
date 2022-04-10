#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* for fgets */
#include <strings.h>     /* for bzero, bcopy */
#include <unistd.h>      /* for read, write */
#include <sys/socket.h>  /* for socket use */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>

#include "Header.h"
#include "Cacheip.h"

void add_to_ip_cache(struct hostent *remoteHost, char * hostname){
    char * IP = (char *)remoteHost->h_addr_list[0];

    struct cache_ip * hostip = (struct cache_ip *)malloc(sizeof(struct cache_ip));
    hostip->next = head;
    hostip->hostname=hostname;
    hostip->IP = IP;
    hostip->h_length = remoteHost->h_length;
    head = hostip;

    printf("IP host mapping added to cache.\n");
}
    
char * get_ip_from_hostname_cache(char * hostname){
    if(head == NULL){
        printf("Host NOT found in cache.\n");
        return NULL;
    }

    // search cache for hostname
    struct cache_ip * crawl = head;
    while(head != NULL){
        printf("Hostname: %s\n", crawl->hostname);
        if(strcasecmp(crawl->hostname, hostname) == 0){
            printf("Host found in cache.\n");
            //bcopy((char *)remoteHost->h_addr_list[0], crawl->IP, remoteHost->h_length);
            return crawl->IP;
        }
        crawl = crawl->next;
    }

    // if not found
    printf("Host NOT found in cache.\n");
    return NULL;
}

short * get_hlength_from_hostname_cache(char * hostname){
    if(head == NULL){
        return NULL;
    }

    // search cache for hostname
    struct cache_ip * crawl = head;
    while(head != NULL){
        if(strcasecmp(crawl->hostname, hostname) == 0){
            //bcopy((char *)remoteHost->h_addr_list[0], crawl->IP, remoteHost->h_length);
            return crawl->h_length;
        }
        crawl = crawl->next;
    }

    // if not found
    printf("Host NOT found in cache.\n");
    return NULL;
}

void delete_cache(){
    if(head == NULL){
        return;
    }

    struct cache_ip * temp;
    while(head != NULL){
        temp = head;
        head = head->next;
        free(temp->hostname);
        free(temp->IP);
        free(temp);
    }
    return;
}