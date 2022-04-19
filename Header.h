#include <fcntl.h>
#include "Cacheip.h"
#ifndef _HEADER_H_
#define _HEADER_H_

struct arg_struct {
    int arg1;
    int arg2;
};

int timeout;


struct ReceiveHeader {
    // required fields
    char * req_method;  // GET or POST
    char * req_uri;     // filepath
    char * httpversion;    //1 for 1.1, 0 for 1.0

    // optional fields
    char * host;
    char * content_length; // POST: number of characters to read in content
    char * connection;   // Keep-alive or not
    char * content_type; 
    char * msg;     // body
    int * port;
    char * path;
};

struct SendHeader {
    // required fields
    char * httpversion;   

    // optional fields
    char * connection;   // Keep-alive or not
    char * content_type;
    char * status_code;  
    char * content_length; // POST: number of characters to read in content
    char * status_msg; // "OK"
    char * msg ; // body
};

struct ReceiveHeader * new_default();
struct SendHeader * send_default();
struct SendHeader * error();

void * timeout_thread(void * path);
void add_to_cache(char * msg, int msg_bytes, struct ReceiveHeader *clientrec);
void get_error_header(struct SendHeader * head_val);
int check_and_handle_valid_http_request(char * request_msg, struct ReceiveHeader * receive_header, struct SendHeader * send_head, struct hostent *remoteHost);
void uri_parsing(char *uri, char *hostname, char *path, int *port);

int open_sendfd(char *hostname, int port);
int open_listenfd(int port);

void echo(int connfd, int * keep_alive_ptr);
void *thread(void * args);
char * put_in_header(char * token, int * index);
void create_header(struct SendHeader * send_header, struct ReceiveHeader * head, FILE * fp);
char * header_to_buf(struct SendHeader * send_header);
char * handle_request(int connfd, char * msg, char *return_msg,  char * filename, int * is_filepat, int *is_post, int * keep_alive_ptr);

FILE * search_cache(struct ReceiveHeader *clientrec, FILE * cached_page);
int get_file(FILE * fp, char * buf, int connfd, int i);
const char *get_filename_ext(const char *filename);
int checkIfFileExists(char * filename);

char * get_content_type(const char *filename);
char * get_content_length(FILE * fp);
void parse_header(char * request_msg, struct ReceiveHeader * head);
void get_forbidden_header(struct SendHeader * head_val);

#endif


