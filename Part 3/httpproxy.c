#define _XOPEN_SOURCE 500
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <string.h>         //memset()
#include <stdio.h>          //sscanf()
#include <unistd.h>         //write()
#include <sys/errno.h>      //errno
#include <sys/stat.h>       //struct stat
#include <fcntl.h>          //open()
#include <pthread.h>        //pthread
#include <signal.h>         //pthread_kill
#include <time.h>           //difftime

#define BUFFER_SIZE 1024000
#define HEADER_SIZE 1000
#define FILENAME_SIZE 260

#define DEBUG 0

// Global string objects for error messages
static const char suc201[] = " 201 Created\r\n";
static const char err400[] = " 400 Bad Request\r\n";
static const char err403[] = " 403 Forbidden\r\n";
static const char err404[] = " 404 File Not Found\r\n";
static const char err500[] = " 500 Internal Server Error\r\n";
static const char err501[] = " 501 Not Implemented\r\n";

/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}

ssize_t send_full(int fd, char *buff, ssize_t size, int filedesc) {
  ssize_t total = 0;
  ssize_t ret = 0;
  
  // Sends error message if error's detected, otherwise sends full content of message
  // Both requires loops of send() until end of buffer
  if (filedesc == -1) {
    while (total < size) {
      ret = send(fd, buff+total, size-total, 0);
      if (ret < 0) {
        if (errno == EAGAIN) {
          continue;
        }
        return ret;
      }
      else if (ret == 0) {
        return total;
      }
      else {
        total += ret;
      }
    }
  }
  else {
    int rb;
    ssize_t offset = 0;
    while (total < size) {
      memset(buff, 0, BUFFER_SIZE);
      rb = pread(filedesc, buff, BUFFER_SIZE, offset);
      ret = send(fd, buff, rb, 0);
      if (ret < 0) {
        if (errno == EAGAIN) {
          continue;
        }
        return ret;
      }
      else if (ret == 0) {
        return total;
      }
      else {
        total += ret;
        offset += ret;
      }
    }
  }
  return total;
}


int is_valid_port(char * port) {

    int namelen = strlen(port);

    for (int i = 0; i < namelen; i++) {
        if (!(port[i] >= '0' && port[i] <= '9')) {
            return 0;
        }
    }
    int port_num = atoi(port);
    if (port_num <= 0 || port_num > 65535) {
        return 0;
    }
    return 1;
}

int is_positive(char * num) {
    int numlen = strlen(num);

    for (int i = 0; i < numlen; i++) {
        if (!(num[i] >= '0' && num[i] <= '9')) {
            return 0;
        }
    }
    int number = atoi(num);
    if (number <= 0) {
        return 0;
    }
    return 1;
}

int is_nonnegative(char * num) {
    int numlen = strlen(num);

    for (int i = 0; i < numlen; i++) {
        if (!(num[i] >= '0' && num[i] <= '9')) {
            return 0;
        }
    }
    int number = atoi(num);
    if (number < 0) {
        return 0;
    }
    return 1;
}

/*
* is_invalid_host()
* checks for an invalid host
*/
int is_invalid_host (char * host) {
    int hostlen = strlen(host);

    for (int i = 0; i < hostlen; i++) {
        //host is invalid when it contains a white space
        if (host[hostlen] == ' ') {
            return 1;
        }
    }
    
    return 0;
}

/*
* is_invalid_resource_name()
* checks for invalid resource (file) name
*/
int is_valid_resource_name (char * name) {
    int namelen = strlen(name);
    
    if (namelen > 19) {
        //if the name is longer than 19 chars, then it's invalid
#if DEBUG == 1
        printf("Name is too long\n");
#endif
        return 0;
    }
    
    for (int i = 0; i < namelen; i++) {
        //if a character is not a-z, A-Z, 0-9, a '.', or '_', then the name is invalid
        if ( !( (name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') || (name[i] == '.') || (name[i] == '_') )) {
#if DEBUG == 1
            printf("Character is bad: %c\n", name[i]);
#endif
            return 0;
        }
    }
    return 1;
}

/*
* is_bad_request()
* checks for a bad request based off the resource(file name), http version, host
*/

int is_bad_request(char * resource, char * httpversion, char * host) {
    if (strcmp(httpversion, "HTTP/1.1") != 0) {
        //http version is invalid
#if DEBUG == 1
        printf("setting to a bad request because of invalid http\n");
#endif
        return 1;
    } else if (!is_valid_resource_name(resource)) {
        //filename is greater than 19 characters or contains invalid characters
#if DEBUG == 1
        printf("setting to a bad request because of invalid characters in filename\n");
#endif
        return 1;
    } else if (host == NULL || is_invalid_host(host)) {
        //host is invalid
#if DEBUG == 1
        printf("setting to a bad request because of invalid host\n");
#endif
        return 1;
    } else {
        return 0;
    }
}

/*
* is_valid_content_length()
* checks for valid content length
*/
int is_valid_content_length (char * start, char * end) {
    ssize_t lengthstrlen = end - start;
    for (int i = 0; i < (int)lengthstrlen; i++) {
        if ( !(start[i] >= '0' && start[i] <= '9') ) {
            return 0;
        }
    }
    return 1;
}


struct httpObject {
    
    char host[FILENAME_SIZE];                     // example: 127.0.0.1:1234
    char method[6];                     // PUT, HEAD, GET
    char filename[FILENAME_SIZE];       // example: file1.txt
    char httpversion[9];                // HTTP/1.1
    ssize_t content_length;             // example: 13
    ssize_t header_length;              // example: 10
    int status_code;                    // example: 404
    uint8_t header[HEADER_SIZE];
    uint8_t buffer[BUFFER_SIZE];
};

/*
 * clear_httpObject()
 * reset all data in an httpObject
 */
void clear_httpObject(struct httpObject* message) {
    memset(message->host, 0, FILENAME_SIZE);
    memset(message->method, 0, 6);
    memset(message->filename, 0, FILENAME_SIZE);
    memset(message->httpversion, 0, 9);
    message->content_length = 0;
    message->header_length = 0;
    message->status_code = 0;
    memset(message->header, 0, HEADER_SIZE);
    memset(message->buffer, 0, BUFFER_SIZE);
}

struct cache_item {
    char * filename;
    struct tm time;
    char * buffer;
};

struct cache {
    struct cache_item * files;
    
    int capacity;
    int max_size;
    
    int current_size;
    int head;
    int tail;
};

void initialize_cache(int s, int m, struct cache* c) {
    c->capacity = s;
    c->max_size = m;
    c->current_size = 0;
    c->head = 0;
    c->tail = 0;
    
    c->files = (struct cache_item *)malloc(sizeof(struct cache_item) * s);
    for (int i=0; i < s; i++) {
        c->files[i].buffer = (char*) malloc (sizeof(char) * m);
        c->files[i].filename = (char*) malloc (sizeof(char) * FILENAME_SIZE);
        memset(c->files[i].buffer, 0, m);
        memset(c->files[i].filename, 0, FILENAME_SIZE);
        memset(&c->files[i].time, 0, sizeof(struct tm));
    }
}

void clear_cache_item(int i, struct cache * c) {
    memset(c->files[i].buffer, 0, c->max_size);
    memset(c->files[i].filename, 0, FILENAME_SIZE);
    memset(&c->files[i].time, 0, sizeof(struct tm));
}

struct parameters {
    int serverfd;
    int optN;
    int optR;
    int client_port;
    struct cache *c;
};

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) {
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    err(EXIT_FAILURE, "socket error");
  }
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) {
    err(EXIT_FAILURE, "bind error");
  }
  if (listen(listenfd, 500) < 0) {
    err(EXIT_FAILURE, "listen error");
  }
  return listenfd;
}

int create_client_socket(uint16_t port) {
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd < 0) {
    return -1;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (connect(clientfd, (struct sockaddr*) &addr, sizeof addr)) {
    return -1;
  }
  return clientfd;
}

void read_http_response(int connfd, struct httpObject* message) {
    memset(message->buffer, 0, BUFFER_SIZE);
    int readcheck = recv(connfd, message->buffer, BUFFER_SIZE, 0);
    char* cont = NULL;
    cont = strstr((char *) message->buffer, "100-continue\r\n");
    
    char* lengthEnd = NULL;
    char* lengthRead = NULL;
    lengthRead = strstr((char *)message->buffer, "Content-Length: ");

    if (lengthRead != NULL) {
        lengthEnd = strstr(lengthRead+16, "\n");
    }
    int finalLength;
    
    char * method = NULL;
    //make a copy of message buffer that we can tokenize
    char * buffercopy;
    buffercopy = calloc(strlen((char *)message->buffer)+1, sizeof(char));
    strcpy(buffercopy, (char *)message->buffer);
    method = strtok(buffercopy, " ");
    
    if (lengthRead != NULL && lengthEnd != NULL) {
        //check for valid content length value, ignore lengthEnd - 1 since that's the null char
        if (is_valid_content_length(lengthRead+16, lengthEnd-1)) {
          sscanf(lengthRead, "Content-Length: %d", &finalLength);

          int total = 0;
          int offset = readcheck;
          while (cont && total < finalLength) {
            readcheck = recv(connfd, message->buffer+offset+total, BUFFER_SIZE, 0);
            total += readcheck;
            cont = strstr((char *) message->buffer+readcheck, "100-continue\r\n");
          }
        } else if (strcmp(method, "PUT") == 0) {
            message->status_code = 501;
        }
    }

    free(buffercopy);
    
    if (readcheck == -1) {
      message->status_code = 500;
    }
    
    char methodRead[6];
    char filenameRead[FILENAME_SIZE];
    char httpversionRead[9];
    char hostRead[FILENAME_SIZE];
    int contentLengthRead = 0;
    
    memset(hostRead, 0, FILENAME_SIZE);
    memset(methodRead, 0, 5);
    memset(filenameRead, 0, FILENAME_SIZE);
    memset(httpversionRead, 0, 9);
    
    sscanf((char *)message->buffer, "%s %s %s\nHost: %s", methodRead, filenameRead, httpversionRead, hostRead);
    strcpy(message->httpversion, httpversionRead);
    strcpy(message->host, hostRead);
    strcpy(message->method, methodRead);
    strcpy(message->filename, ".");
    strcat(message->filename, filenameRead);
    char * clp = NULL;

    clp = strstr((char *) message->buffer, "Content-Length: ");
    if (clp != NULL) {
        sscanf(clp, "Content-Length: %d", &contentLengthRead);
        message->content_length = contentLengthRead;
    }
    if (is_bad_request(message->filename+2, message->httpversion, message->host)) {
             //pass in message->filename+2 to ignore the first 2 chars "./"
             message->status_code = 400;
    }
    else if (strcmp(methodRead, "GET") != 0) {
        message->status_code = 501;
    }
    else if (message->status_code == 400 || message->status_code == 500) {
        return;
    }
    
    return;
}

/*
* construct_http_response()
* Creates message regarding task status
*/
void construct_http_response(struct httpObject* message) {

    char * lengthStr = (char*)malloc(128);
    memset(lengthStr, 0, 128);

    char * statusStart = NULL;
    char * statusEnd = NULL;
    strcat((char *)message->header, message->httpversion);
    
    if (message->status_code == 200) {
      strcat((char *)message->header, " 200 OK\r\n");
      sprintf(lengthStr, "Content-Length: %ld\r\n\r\n", message->content_length);
      strcat((char *)message->header, lengthStr);
    }
    else {
      memset(message->buffer, 0, BUFFER_SIZE);
      switch(message->status_code) {
        case 201:
          strcat((char *)message->header, suc201);
          break;
        case 400:
          strcat((char *)message->header, err400);
          break;
        case 403:
          strcat((char *)message->header, err403);
          break;
        case 404:
          strcat((char *)message->header, err404);
          break;
        case 501:
          strcat((char *)message->header, err501);
          break;
        default:
          strcat((char *)message->header, err500);
          break;
      }
      
      //take the status message part of the header and put that in response body
      //status body = After first occurence of 2nd space through the first occurence of \r\n\r\n
      // i.e: HTTP/1.1 200 Status\r\nContent-....
      statusStart = strstr((char *) message->header, " ");
      if (statusStart != NULL) {
        statusStart +=1;
        statusStart = strstr((char *) statusStart, " ");
      }
      statusEnd = strstr((char *) message->header, "\r");
      if (statusStart != NULL && statusEnd != NULL) {
          size_t responseLength = (statusEnd) - (statusStart+1);
          char * responseStr = (char*)malloc(sizeof(char)*(responseLength+1));
          strncpy(responseStr, statusStart+1, responseLength);
          responseStr[responseLength] = '\0';
          strcat((char*)message->buffer, responseStr);
          strcat((char*)message->buffer, "\n");
      }
      message->content_length = strlen((char *)message->buffer);
      sprintf(lengthStr, "Content-Length: %ld\r\n\r\n", message->content_length);
      strcat((char *)message->header, lengthStr);
        
#if DEBUG == 1
      //printf("My message buffer is currently %s\n", message->buffer);
      //printf("I set message content length to: %zd\n", message->content_length);
      //printf("New message header is: %s\n", (char *)message->header);
#endif
    }
    message->header_length = strlen((char *)message->header);
    free(lengthStr);
    return;
}

/*
* send_http_response()
* Delivers status message to client, follow up with content if available
*/
void send_http_response(int connfd, struct httpObject* message) {
    write(connfd, message->header, message->header_length);
    if (message->status_code != 200) {
        // a response body for anything other than 200
        write(connfd, message->buffer, message->content_length);
    }
}

void set_time(char* buffer, struct tm time) {
    char* parse_ptr = strstr(buffer, "Last-Modified: ");
    strptime(parse_ptr+15, "%a, %d %b %Y %H:%M:%S %z", &time);
}

int is_updated(char* fname, int port, struct tm cache_time) {
	//printf("running isupdated\n");
    char buff[HEADER_SIZE];
    memset(buff, 0, HEADER_SIZE);
    sprintf(buff, "HEAD %s HTTP/1.1\r\nHost: localhost:%d\r\n\r\n", fname+1, port);
	int connfd = create_client_socket(port);
    send(connfd, buff, strlen(buff), 0);
    memset(buff, 0, HEADER_SIZE);
    recv(connfd, buff, HEADER_SIZE, 0);
	struct tm fetched_time;
    memset(&fetched_time, 0, sizeof(struct tm));
	char* parse_ptr = strstr((char*)buff, "Last-Modified: ");
//printf("crashh\n");
//printf("buff=%s\n",buff);
//printf("parse_ptr=%s\n",parse_ptr);
    strptime(parse_ptr+15, "%a, %d %b %Y %H:%M:%S %z", &fetched_time);
//printf("crashh\n");
    time_t cache_time_t = mktime(&cache_time);
    time_t fetched_time_t = mktime(&fetched_time);
    //printf("compare time\n");
    int result = difftime(cache_time_t, fetched_time_t);
    if (result >= 0) {
        return 1;
    }
    else {
        return 0;
    }
}

int read_cache(struct httpObject* message, struct cache * c, int port) {
	//printf("read cache\n");    
	if (c->current_size == 0 || c->max_size == 0 || c->capacity == 0) {
        return 0;
    }
    else {
	//printf("start for loop\n");
        for (int i=0; i < c->current_size; i++) {
            if (strcmp(message->filename, c->files[i].filename) == 0 && is_updated(c->files[i].filename, port, c->files[i].time)) {
                memset(message->buffer, 0, BUFFER_SIZE);
                strcpy((char*)message->buffer, c->files[i].buffer);
                return 1;
            }
        }
    }
    return 0;
}

void write_cache(struct httpObject* message, struct cache * c) {
    //printf("write cache\n");
    int index = -1;
    if ((int)strlen((char*)message->buffer) > c->max_size) {
        return;
    }
    else if (c->current_size == c->capacity) {
        index = c->head;
        clear_cache_item(index, c);
        strcpy(c->files[index].buffer, (char*)message->buffer);
        strcpy(c->files[index].filename, (char*)message->filename);
        set_time((char*)message->buffer, c->files[index].time);
        c->tail += 1;
        c->head += 1;
        if (c->tail == c->capacity) {
            c->tail = 0;
        }
        if (c->head == c->capacity) {
            c->head = 0;
        }
    }
    else if (c->current_size < c->capacity) {
        index = c->current_size;
        strcpy(c->files[index].buffer, (char*)message->buffer);
        strcpy(c->files[index].filename, (char*)message->filename);
        set_time((char*)message->buffer, c->files[index].time);
        c->tail = index;
	    c->current_size += 1;
    }
}


void connect_server(int clientfd, int serverfd, char *buffer) {
	send(clientfd, buffer, strlen(buffer), 0);
    memset(buffer, 0, BUFFER_SIZE);
//printf("starting receive\n");
	recv(clientfd, buffer, BUFFER_SIZE, 0);
    //printf("Buffer:\n---\n%s\n---\n", buffer);
	send_full(serverfd, buffer, BUFFER_SIZE, -1);
}


void handle_connection(void * pargs) {

    struct parameters * args = (struct parameters *) pargs;
    int serverfd = args->serverfd;
    struct cache * c = args->c;
    
    struct httpObject message;
    clear_httpObject(&message);
    
    read_http_response(serverfd, &message);
    if (message.status_code == 400 || message.status_code == 500 || message.status_code == 501) {
        construct_http_response(&message);
        send_http_response(serverfd, &message);
    }
    else if (read_cache(&message, c, args->client_port) == 1) {
	//printf("Getting from cache\n");
        send_full(serverfd, (char*)message.buffer, strlen((char*)message.buffer), -1);
    }
    else {
        int clientfd = create_client_socket(args->client_port);
        if (clientfd == -1) {
            message.status_code = 500;
            construct_http_response(&message);
            send_http_response(serverfd, &message);
        }
        else {
		//printf("Connecting to server\n");            
		connect_server(clientfd, serverfd, (char *)message.buffer);
        	char status[3];
		sscanf((char*)message.buffer,"HTTP/1.1 %s", status);
		//printf("Server completed with status code %s\n", status);
		if (strcmp(status, "200") == 0 && c->max_size != 0 && c->capacity != 0) {    
			//printf("Writing to cache\n");			
			write_cache(&message, c);
		}
        }
        close(clientfd);
    }
    
  	close(serverfd);
	//printf("Ending handle()...\n");
}

int run_healthcheck(int * array, int size) {
    int connfd = 0;
    int error = 9999;
    int entry = 9999;
    int best_port = array[0];
    int error_read = 9999;
    int entry_read = 9999;
	char* parse_ptr;
	//char* content_length;
    
    char request_message[HEADER_SIZE];
    for (int i=0; i < size; i++) {
        memset(request_message, 0, HEADER_SIZE);
        sprintf(request_message, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\n\r\n", array[i]);
        connfd = create_client_socket(array[i]);
        if (connfd > 0) {
            send(connfd, request_message, strlen(request_message), 0);
            memset(request_message, 0, HEADER_SIZE);
            recv(connfd, request_message, HEADER_SIZE, 0);
		//printf("request_message: %s\n", request_message);
		parse_ptr = strstr((char*) request_message, "\r\n\r\n");           
		sscanf(parse_ptr+4, "%d\n%d\n", &error_read, &entry_read);
		//printf("Error/Entry: %d / %d \n", error_read, entry_read);
            if (entry_read < entry) {
                best_port = array[i];
                entry = entry_read;
                error = error_read;
            }
            else if (entry_read == entry) {
                if (error_read < error) {
                    best_port = array[i];
                    entry = entry_read;
                    error = error_read;
                }
            }
        }
        memset(request_message, 0, HEADER_SIZE);
        close(connfd);
    }
    
    return best_port;
}

int main(int argc, char *argv[]) {
    int listenfd;
    uint16_t port;
    
    struct parameters args;
    args.optN = 5;
    args.optR = 5;
    args.client_port = 0;
    int opts = 3;
    int optm = 1024;
    int clients_count = argc - 2;

    int opt;
    while ((opt = getopt(argc, argv, "N:R:s:m:")) != -1) {
        switch (opt) {
            case 'N':
                if (is_positive(optarg) != 1 ) {
                    errx(EXIT_FAILURE, "invalid number of parallel connections: -N (%s)", optarg);
                    exit(EXIT_FAILURE);
                }
                else {
                    args.optN = atoi(optarg);
                    clients_count = clients_count - 2;
                }
                break;
            case 'R':
                if (is_positive(optarg) != 1 ) {
                    errx(EXIT_FAILURE, "invalid number of healthcheck frequency: -R (%s)", optarg);
                    exit(EXIT_FAILURE);
                }
                else {
                    args.optR = atoi(optarg);
                    clients_count = clients_count - 2;
                }
                break;
            case 's':
                if (is_nonnegative(optarg) != 1 ) {
                    errx(EXIT_FAILURE, "invalid cache capacity: -s (%s)", optarg);
                    exit(EXIT_FAILURE);
                }
                else {
                    opts = atoi(optarg);
                    clients_count = clients_count - 2;
                }
                break;
            case 'm':
                if (is_nonnegative(optarg) != 1 ) {
                    errx(EXIT_FAILURE, "invalid maximum cach file size: -m (%s)", optarg);
                    exit(EXIT_FAILURE);
                }
                else {
                    optm = atoi(optarg);
                    clients_count = clients_count - 2;
                }
                break;
            default:
                fprintf(stderr, "Usage: %s port [-N connections] [-R rate_of_healthcheck] [-s cache_capacity] [-m max_cache_size] servers...\n", argv[0]);
                exit(EXIT_FAILURE);
            }
    }
    if (clients_count == 0) {
        errx(EXIT_FAILURE, "Missing server ports");
        exit(EXIT_FAILURE);
    }
    //else if (argv[1] == NULL) {
    else if (clients_count < 0) {
        errx(EXIT_FAILURE, "Usage: %s port [-N connections] [-R rate_of_healthcheck] [-s cache_capacity] [-m max_cache_size] servers...\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    else {
        if (is_valid_port(argv[optind]) != 1) {                                //optind => 1
            errx(EXIT_FAILURE, "invalid port number: %s", argv[optind]);    //optind => 1
            exit(EXIT_FAILURE);
        }
        else {
            port = strtouint16(argv[optind]);                                //optind =>1
        }
    }
    int * client_port_array = (int *) malloc(sizeof(int) * clients_count);
    //int client_port_aray[clients_count];
    int PORT_INDEX = 0;
    
    if (optind < argc) {
        while (optind < argc) {
            if(is_valid_port(argv[optind]) != 1) {
                errx(EXIT_FAILURE, "invalid port number: %s", argv[optind]);
                exit(EXIT_FAILURE);
            }
            else {
                if (PORT_INDEX != 0) {
                    client_port_array[PORT_INDEX-1] = atoi(argv[optind]);
                }
                PORT_INDEX++;
                optind++;
            }
        }
    }
    
    listenfd = create_listen_socket(port);
    int request_count = 0;
    struct cache * c = (struct cache *)malloc(sizeof(struct cache));
    initialize_cache(opts, optm, c);
    args.c = c;

    while(1) {
        if (request_count % args.optR == 0) {
            args.client_port = run_healthcheck(client_port_array, clients_count);
            //printf("Port of choice = %d\n", args.client_port);
        }
        args.serverfd = accept(listenfd, NULL, NULL);
        //printf("serverfd = %d\n", args.serverfd);
        if (args.serverfd < 0) {
            //printf("This ended the connection\n");
            warn("accept error");
            continue;
        }
        handle_connection((void*)&args);
        request_count++;
    }
    //printf("This ended the connection\n");
    return EXIT_SUCCESS;
}
