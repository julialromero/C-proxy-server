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
#include <errno.h>

#include "Header.h"
#include "Cacheip.h"

#define MAXLINE  8192  /* max text line length */
#define MAXBUF   8192  /* max I/O buffer size */
#define LISTENQ  1024  /* second argument to listen() */
#define MAXFILEBUF 100000

int open_listenfd(int port);
void receive_from_client(int connfd);
void *thread(void *vargp);

int main(int argc, char **argv) 
{
    head = NULL;
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid; 

    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }
    port = atoi(argv[1]);

    listenfd = open_listenfd(port);
    while (1) {
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);
        pthread_create(&tid, NULL, thread, connfdp);
    }

    delete_cache();
}

/* thread routine */
void * thread(void * vargp) 
{  
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);

    receive_from_client(connfd);

    close(connfd);
    printf("Connection closed.\n\n");
    return NULL;
}


void receive_from_client(int connfd) 
{
    size_t n; 
    char buf[MAXBUF]; 
    bzero(buf, MAXBUF);

    char clientbuf[MAXBUF]; 
    bzero(clientbuf, MAXBUF);

    n = read(connfd, buf, MAXBUF);
    strcpy(clientbuf, buf);
    //printf("Received msg: %s\n", buf);

    // declare variables
    struct ReceiveHeader receive_header;
    struct SendHeader send_head;
    struct hostent *remoteHost;
    struct sockaddr_in clientaddr;
    receive_header.port = -1;


    int i = check_and_handle_valid_http_request(buf, &receive_header, &send_head, &remoteHost);
    // if invalid request return error message
    if(i == 0){
        bzero(buf, MAXBUF);
        char * t = header_to_buf(&send_head);
        strcpy(buf, t);  
        write(connfd, buf, strlen(buf));
        return;
    }
   
    FILE * cached_page = NULL;
    cached_page = search_cache(&receive_header, cached_page);
    
    if(cached_page != NULL){
        bzero(buf, MAXBUF);

        get_file(cached_page, buf, connfd, 1);
        return;
    }

    // connect to server
    char hostname[MAXBUF];
    char path[MAXBUF];
    int port = -1;
    uri_parsing(receive_header.req_uri, hostname, path, &port);
    receive_header.path = path;
    receive_header.host = hostname;

    printf("Page is not in cache. Sending request to server...%s\n", receive_header.host);
    int serverfd = open_sendfd(receive_header.host, receive_header.port);
    if (serverfd == -1){
        bzero(buf, MAXBUF);
        char * t = header_to_buf(&send_head);
        strcpy(buf, t);  
        write(connfd, buf, strlen(buf));
        return;
    }

    // send request to server
    i = write(serverfd, clientbuf, strlen(clientbuf));
    if(i < 0){
        close(serverfd);
        bzero(buf, MAXBUF);
        char * t = header_to_buf(&send_head);
        strcpy(buf, t);  
        write(connfd, buf, strlen(buf));
        return;
    }
    
    char cache_buf[MAXFILEBUF];
    bzero(cache_buf, MAXFILEBUF);
    // read from socket
    bzero(buf, MAXBUF);
    
    int size = 0;
    while((n = read(serverfd, buf, MAXBUF)) > 0){
        memcpy(cache_buf + size, buf, n);
        size+=n;

        write(connfd, buf, n);
    }

    printf("Bytes received: %d\n\n", size);
    close(serverfd);
    
    //store file in cache
    add_to_cache(cache_buf, size, &receive_header);
}

/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0)
        return -1;

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
} /* end open_listenfd */

/* 
 * open_sendfd -
 * Returns -1 in case of failure 
 */
int open_sendfd(char *hostname, int port){
    
    int serverfd, optval=1;;
    struct sockaddr_in serveraddr;
    struct hostent rh;
    struct hostent *remoteHost = &rh;

    // // open new socket to connect to server
    // if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
	// 	return -1;
	// }

    // hostent *remoteHost = get_ip_from_hostname_cache(hostname, remoteHost);
    // if(IP == NULL){
    //     printf("Querying hostname: %s\n", hostname);
    //     remoteHost = gethostbyname(hostname);
    //     if(remoteHost == NULL){
    //         return -1;
    //     }

    //     // set request address
    //     bzero((char *) &serveraddr, sizeof(serveraddr));
    //     bcopy((char *)remoteHost->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, remoteHost->h_length);

    //     // add to Hostname IP cache
    //     add_to_ip_cache(remoteHost, hostname);
    // }
    // else{
    //     bzero((char *) &serveraddr, sizeof(serveraddr));
    //     bcopy((char *)IP, (char *)&serveraddr.sin_addr.s_addr, strlen(IP));
    // }


    // if hostname not in cache, resolve and add to cache
    char * IP = get_ip_from_hostname_cache(hostname);
    if(IP == NULL){
        printf("Querying hostname: %s\n", hostname);
        remoteHost = gethostbyname(hostname);
        if(remoteHost == NULL){
            return -1;
        }

        // set request address
        bzero((char *) &serveraddr, sizeof(serveraddr));
        bcopy((char *)remoteHost->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, remoteHost->h_length);

        // add to Hostname IP cache
        add_to_ip_cache(remoteHost, hostname);
    }
    else{
        hlength = get_hlength_from_hostname_cache(hostname)
        bzero((char *) &serveraddr, sizeof(serveraddr));
        bcopy((char *)IP, (char *)&serveraddr.sin_addr.s_addr, hlength);
    }

    // construct server struct
    serveraddr.sin_family = AF_INET;   
    if(port > 0){
        serveraddr.sin_port = htons(port);
    }
    else{
        serveraddr.sin_port = htons(80);
    }
 
    // printf("Hostname: %s\n", remoteHost->h_name);
    // printf("Address: %d\n", remoteHost->h_addr_list[0]);

    // establish connection
    int sockid = connect(serverfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    if (sockid < 0){
		return -1;
	}

    //printf("Socket connected\n");

    return serverfd;
}

/* function to check if HTTP method is valid (GET)
    1) check if HTTP server at the requested URL exists --> call gethostbyname()
    2) if not met, send 400 bad request error message
    3) if met, forward request
    - return 1 if valid, 0 if invalid
*/
int check_and_handle_valid_http_request(char * request_msg, struct ReceiveHeader * receive_header, struct SendHeader * send_head, struct hostent *remoteHost){
    // extract info from header
    parse_header(request_msg, receive_header);

    char hostname[MAXBUF];
    char path[MAXBUF];
    int port = -1;
    uri_parsing(receive_header->req_uri, hostname, path, &port);
    // check if valid request
    
    receive_header->host = hostname;
    receive_header->port = port;
    receive_header->path = path;

    // if no hostname, populate error, return 
    if(hostname == NULL){
        get_error_header(send_head);
        return 0;
    }

    printf("Requested URI:: %s\n", receive_header->req_uri);
    remoteHost = gethostbyname(hostname);

    // if host doesn't exist, populate error, return 
    if(remoteHost == NULL){
        printf("Remote host not found\n");
        get_error_header(send_head);
        return 0;
    }
    // printf("Remote host found.\n");
    
    // if valid, return true (1)
    return 1;

}


/* function to parse URL request and extract:
    1) requested host and port number, default 80
    2) requested path
    3) optional message body
*/
void parse_header(char * request_msg, struct ReceiveHeader * head){
    const int num_args = 8;
    const int num_temp = 5;
    
    char *cmdarray[num_args] = {NULL};
    char * temparray[num_temp] = {NULL}; // indices correspond to: content_length, connection, content_type, msg, host 
    char * del = "\r\n\r\n";
    
    // initial split to separate header and POST data
    char * cpy = strdup(request_msg);
    char * token = strstr(cpy, del); 
    if(token != NULL){
        cmdarray[6] = strdup(token);
        //printf("Length: %d\n", strlen(cmdarray[6]));
    }
    else{
        cmdarray[6] = NULL;
    }
    token = strtok_r(request_msg, " ", &request_msg);
    cmdarray[0] =strdup(token);
 
    token = strtok_r(request_msg, " ", &request_msg); 
    cmdarray[1] =strdup(token);
 
    token = strtok_r(request_msg, "\r\n", &request_msg);
    cmdarray[2] =strdup(token);
   

    token = strtok_r(request_msg, "\r\n", &request_msg); 
    int i = 0;
    while(token != NULL & i < num_temp) {
        temparray[i] = strdup(token);
        i++;
        token = strtok_r(request_msg, "\r\n", &request_msg); 
    }
    
    char * temp;
    int index = 0;
    for(i=0; i < num_temp; i++){
        if (temparray[i] == NULL){
            break;
        }
        temp = put_in_header(temparray[i], &index);
        if(temp == NULL){
            continue;
        }
        cmdarray[index] = strdup(temp);
    }
   

    head->req_method = cmdarray[0];
    head->req_uri = cmdarray[1];
    head->httpversion = cmdarray[2];
    head->content_length = cmdarray[3];
    head->connection = cmdarray[4];
    head->content_type = cmdarray[5];
    
    char extracted_msg[MAXBUF];
    strcpy(extracted_msg, cmdarray[6]);
    strcat(extracted_msg, "\0");
    head->msg = malloc(sizeof(extracted_msg));
    head->msg = extracted_msg;

    //head->host = cmdarray[7];
    head->host = "none";
    head->path = "none";

    if(strcasecmp(head->req_uri, "/inside") == 0){
        bzero(head->req_uri, MAXBUF);
        strcpy(head->req_uri, "/index.html");
    }
    if(head->content_length !=NULL){
        printf("Content length extracted: %s\n", head->content_length);
    }
    
    return;
}

// extract the optional header and the value, put in header struct
// update index: 1-content_length, 2-connection, 3-content_type, 4-msg 
char * put_in_header(char * token, int * index){
    // printf("Func: %s\n", token);
    char * field = strtok_r(token, " ", &token);
    
    if(field == NULL){
        return NULL;
    }
    else{
        if (strcmp(field, "Connection:") == 0){
           char * val = strtok_r(token, "\r\n", &token);
           *index = 1+3;
            return strdup(val);
        }
        if (strcmp(field, "Content-Length:") == 0){
            char * val = strtok_r(token, "\r\n", &token);
            *index = 0+3;
            return strdup(val);
        }
        if (strcmp(field, "Content-Type:") == 0){
            char * val = strtok_r(token, "\r\n", &token);
            *index = 2+3;
            return strdup(val);
        }
        if (strcmp(field, "Host:") == 0){
            char * val = strtok_r(token, "\r\n", &token);
            *index = 7;
            return strdup(val);
        }
    }
    return NULL;
}

