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

#define MAXBUF   8192  /* max I/O buffer size */
#define SMALLBUF  1000
#define LISTENQ  1024
#define MAXFILEBUF 10000

char * hash_func(unsigned char *str){
    unsigned long hash = 5381;
    int c;
    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    char hashstr[MAXBUF];
    sprintf(hashstr, "%lu", hash);
    return hashstr;
}

// add to cache
void add_to_cache(char * msg, int msg_bytes, struct ReceiveHeader *clientrec){
    char filename[MAXFILEBUF];
    bzero(filename, MAXFILEBUF);

    strcpy(filename, clientrec->req_uri);
    char * hash = hash_func(filename);
      
    char **endptr;
    // printf("Length saving: %d\n", msg_bytes);

    // printf("\n\nMessage:\n%s\n\n", msg);
    // printf("Length of message: %d\n", len2);
    char path[MAXBUF]; 
    bzero(path, MAXBUF);
    strcpy(path, "cache/"); 
    strcat(path, hash);

    FILE * fp;
    fp = fopen(path, "wb");

    fwrite(msg, 1, msg_bytes, fp);
    fclose(fp);
    printf("Webpage added to cache.\n");

    if(timeout != -1){
        pthread_t tid;
        char * mempath = malloc(strlen(path));
        bzero(mempath, strlen(path));
        strcpy(mempath, path);
        pthread_create(&tid, NULL, timeout_thread, mempath);
    }
}



/*
    ADAPTED FROM https://github.com/sadiredd-sv/Proxy--Caching-web-proxy/blob/master/proxy.c
*/
void uri_parsing(char *uri, char *hostname, char *path, int *port) {	
	*port = 0;
	strcpy(path, "/");
    char *temp;

	if((temp = strpbrk(strpbrk(uri, ":") + 1, ":"))){
		sscanf(temp, "%d %s", port, path);
		sscanf(uri, "http://%[^:]s", hostname);
	} 
    
    else {
		sscanf(uri, "http://%[^/]s/", hostname);
		sscanf(uri, "http://%*[^/]%s", path);
	}

	if((*port) == 0){
		*port = 80;
	}
}

/* function to search cache for the uri
    1) search local directory to see if file exists
    2) if doesn't exist, return
    3) if does exist, return file pointer
    - return 1 if valid, 0 if invalid
*/
FILE * search_cache(struct ReceiveHeader *clientrec, FILE * cached_page)
{   
    char * hash = hash_func(clientrec->req_uri);
    
    char path[MAXBUF]; 
    bzero(path, MAXBUF);
    strcpy(path, "cache/");    
    

    strcat(path, hash);
    cached_page = fopen(path, "rb");

    if (cached_page != NULL)
    {
        printf("File found in cache.\n");
        return cached_page;
    }
    printf("File NOT found in cache.\n");
    return NULL;
}

void create_header(struct SendHeader * send_header, struct ReceiveHeader * head, FILE * fp){
    // set header fields
    send_header->content_type = get_content_type(head->req_uri);
    send_header->content_length = get_content_length(fp);
    send_header->httpversion = head->httpversion;
    send_header->status_code = "200";
    send_header->status_msg = "OK";
    send_header->connection = "none";
    send_header->msg = "none";


    // if received header has Keep-alive, then include in send header
    if(head->connection != NULL){
        if(strcmp("Keep-alive", head->connection) == 0){
            send_header->connection = "Keep-alive";     
        }
    }
    else{
            send_header->connection = "Close";
        }
}

void get_error_header(struct SendHeader * head_val){
    head_val->httpversion = "HTTP/1.1";
    head_val->connection = "none";
    head_val->content_type = "none";
    head_val->status_code = "400";
    head_val->content_length = "none";
    head_val->status_msg = "Internal Server Error";
    head_val->msg = "none";
}

char * get_header(char * s, char * field){
    if(strcmp("none", s) == 0){
        char * ret = "";
        return ret;
    }
    else{
        
        if(strcmp("", field) == 0){
            return s;
        }
        char ret[MAXBUF];
        bzero(ret, MAXBUF);
        strcpy(ret, field);
        strcat(ret, s);
        return ret;    
    }
}

// helpers
char * header_to_buf(struct SendHeader * head){
    // httpversion, connection, content_type, status_code, content_length, status_msg, msg
    char buf[MAXBUF];
    bzero(buf, MAXBUF);
    // first line
    char * s1 = head->httpversion;
    char * s2 = head->status_code;
    char * s3 = head->status_msg;

    // headers
    char * s4 = strdup(get_header(head->connection, &"Connection: "));
    // printf("\nhead->content_type; %s\n",head->content_type);
    char * s5 = strdup(get_header(head->content_type, &"Content-Type: "));
    char * s6 = strdup(get_header(head->content_length, &"Content-Length: "));
    // char * s7 = strdup(get_header(head->msg, ""));

    snprintf(buf, MAXBUF, "%s %s %s", s1, s2, s3);
    if(strcmp("", s4) != 0){
        snprintf(buf, MAXBUF, "%s\r\n %s", buf, s4);
    }
    if(strcmp("", s5) != 0){
        snprintf(buf, MAXBUF,"%s\r\n %s", buf, s5);
    }
    if(strcmp("", s6) != 0){
        snprintf(buf, MAXBUF, "%s\r\n %s", buf, s6);
    }
    return buf;
}

// returns 1 if successful, returns 0 if not
int get_file(FILE * fp, char * buf, int connfd, int i)
{
    printf("Sending file\n");
    // if(strcasecmp(path, "www/") == 0){
    //     bzero(path, MAXBUF);
    //     strcpy(path, "www/index.html");
    // }

    int num_bytes;
    while(1){
        bzero(buf, MAXBUF);
        num_bytes = fread(buf, 1, MAXBUF, fp);
        // printf("Num bytes sending: %d\n", num_bytes);

        // stop if end of file is reached
        if (num_bytes == 0){ 
            break;
        }
        if (i == 0){
            char * headerspace = "\r\n\r\n";
            write(connfd, headerspace, strlen(headerspace));
            i++;
        }
        // send file chunk
        write(connfd, buf, num_bytes);
    }
    return 1;
}

int checkIfFileExists(char * filename)
{
    
    FILE *file;
    if ((file = fopen(filename, "rb")))
    {
        fclose(file);
        return 1;
    }

    return 0;
}

const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

char * get_content_type(const char *filename){
    char * ext = get_filename_ext(filename);
    if(strcmp("html", ext) == 0){
        return "text/html";
    }
    else if(strcmp("txt", ext) == 0){
        return "text/plain";
    }
    else if(strcmp("png", ext) == 0){
        return "image/png";
    }
    else if(strcmp("gif", ext) == 0){
        return "image/gif";
    }
    else if(strcmp("jpg", ext) == 0){
        return "image/jpg";
    }
    else if(strcmp("css", ext) == 0){
        return "text/css ";
    }
    return "text/plain";
}

char * get_content_length(FILE * fp){
    // get size of file
    fseek(fp, 0L, SEEK_END);
    int res = ftell(fp);
    // printf("Total size of requested file = %d bytes\n", res);

    // convert to char *
    char *len = malloc(SMALLBUF);
    bzero(len, SMALLBUF);
    snprintf(len, SMALLBUF, "%d", res);

    fseek(fp, 0L, SEEK_SET);
    return len;
}