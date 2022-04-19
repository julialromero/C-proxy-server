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
#define MAXFILEBUF 60000

int open_listenfd(int port);
void receive_from_client(int connfd, int *timeout);



int main(int argc, char **argv) 
{
    head = NULL;
    blacklisthead = NULL;
    load_blacklist();
    struct arg_struct args;
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid; 
    timeout = -1;

    if ((argc != 2) & (argc != 3)) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }
    port = atoi(argv[1]);
    
    if(argc == 3){
        timeout = atoi(argv[2]);
    }

    listenfd = open_listenfd(port);
    args.arg2 = timeout;
    while (1) {
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);
        args.arg1 = *connfdp;
        
        pthread_create(&tid, NULL, thread, (void *)&args);
    }

    delete_cache();
    delete_blacklisted();
}

/* thread routine for managing cache and implementing timeout */
void * timeout_thread(void * args) 
{  
    char * arg = (char *)args;
    char path[MAXBUF];
    bzero(path, MAXBUF);
    strcpy(path, arg);
    printf("Path: %s\n", path);
    pthread_detach(pthread_self()); 
    free(args);

    time_t expiration = timeout + time(NULL);
    time_t now;
    while(1){
        now = time(NULL);
        if(now >= expiration){
            if(remove(path) == 0){
                printf("File deleted from cache.\n");
            }
        }
    }
   
    return NULL;
}

/* thread routine */
void * thread(void * argument) 
{  
    struct arg_struct *args = argument;

    int connfd = args->arg1;
    int timeout = args->arg2;
    pthread_detach(pthread_self()); 
    //free(argument);

    receive_from_client(connfd, timeout);

    close(connfd);
    printf("Connection closed.\n\n");
    return NULL;
}

void receive_from_client(int connfd, int *timeout) 
{
    size_t n; 
    char buf[MAXBUF]; 
    bzero(buf, MAXBUF);

    char clientbuf[MAXBUF]; 
    bzero(clientbuf, MAXBUF);

    n = read(connfd, buf, MAXBUF);
    strcpy(clientbuf, buf);

    // declare variables
    struct ReceiveHeader receive_header;
    struct SendHeader send_head;
    struct hostent *remoteHost;
    struct sockaddr_in clientaddr;


    printf("Checking if valid\n");
    int i = check_and_handle_valid_http_request(buf, &receive_header, &send_head, remoteHost);
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
    receive_header.port = &port;

    printf("Page is not in cache.\n");
    int serverfd = open_sendfd(receive_header.host, *receive_header.port);
    if (serverfd < 0){
        printf("Failed - sending 404\n");
        bzero(buf, MAXBUF);
        get_error_header(&send_head);
        char * t = header_to_buf(&send_head);
        strcpy(buf, t);  
        write(connfd, buf, strlen(buf));
        return;
    }

    printf("Sending request to server...\n");
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
    
    // char cache_buf[MAXFILEBUF];
    // bzero(cache_buf, MAXFILEBUF);
    char *cache_buf = calloc(MAXFILEBUF,1);

    // read from socket
    bzero(buf, MAXBUF);
    
    int size = 0;
    while((n = read(serverfd, buf, MAXBUF)) > 0){
        write(connfd, buf, n);
        
        // make sure allocated memory is large enough
        if((cache_buf = realloc(cache_buf, size + n*sizeof(char))) == NULL){
            printf("Error allocating memory to file\n");
            close(serverfd);
            bzero(buf, MAXBUF);
            char * t = header_to_buf(&send_head);
            strcpy(buf, t);  
            write(connfd, buf, strlen(buf));
            return;
        }

        memcpy(cache_buf + size, buf, n);
        size+=n;
        printf("Byte sum: %d\n\n", size);   
        
    }

    printf("Bytes received: %d\n\n", size);
    close(serverfd);
    
    //store file in cache
    add_to_cache(cache_buf, size, &receive_header);

    free(cache_buf);
    free(receive_header.connection);
    free(receive_header.content_length);
    free(receive_header.content_type);
    //free(receive_header.host);
    free(receive_header.httpversion);
    //free(receive_header.path);
    //free(receive_header.port);
    free(receive_header.req_method);
    //free(receive_header.req_uri);
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

    /* Create a socket descriptor */
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

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
        short * hlength = get_hlength_from_hostname_cache(hostname);
        bzero((char *) &serveraddr, sizeof(serveraddr));
        bcopy((char *)IP, (char *)&serveraddr.sin_addr.s_addr, hlength);
    }

    // construct server struct
    serveraddr.sin_family = AF_INET;  
     
    if(port > 0){
        serveraddr.sin_port = htons(port);
    }
    else{
        
        serveraddr.sin_port = htons(990);
    }

    // printf("Connecting to socket...\n");
    // establish connection
    int sockid = connect(serverfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    if (sockid < 0){
        perror("Error: ");
        printf("Connection failed\n");
		return -1;
	}

    printf("Socket connected\n");

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
    receive_header->port = &port;
    receive_header->path = path;

    // if no hostname, populate error, return 
    if(strcmp(hostname, "") == 1){
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

    // check if blacklisted
    int i = check_if_blacklisted(remoteHost);
    if( i != 1){
        printf("Site is blacklisted.\n");
        get_forbidden_header(send_head);
        return 0;
    }
    
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

