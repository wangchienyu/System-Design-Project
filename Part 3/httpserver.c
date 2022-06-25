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

#define BUFFER_SIZE 1024000
#define HEADER_SIZE 1000
#define FILENAME_SIZE 260
#define LOG_SIZE 2600     // 5 + 1 + 256 + 1 + 256 + 1 + 5 + 1 + 2000 + 1
#define QUEUE_SIZE 100

#define DEBUG 0

// Global string objects for error messages
static const char suc201[] = " 201 Created\r\n";
static const char err400[] = " 400 Bad Request\r\n";
static const char err403[] = " 403 Forbidden\r\n";
static const char err404[] = " 404 File Not Found\r\n";
static const char err500[] = " 500 Internal Server Error\r\n";
static const char err501[] = " 501 Not Implemented\r\n";

/*
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

/*
    Struct httpObject
    Ａ struct object to categorize and store the components passed within an HTTP message
    
    ***Citation***
    This design was adapted from the sample starter code of my previous CSE130 class, as follows:
 
    Title: server.c source code
    Author: Michael C (@mdcovarr)
    Date: 10/18/2021
    Code version: Github
    Availability: https://git.ucsc.edu/mdcovarr/cse130-section/-/blob/master/week-4/server.c
    ***End of Citation***
*/
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
    char log_body_buffer[BUFFER_SIZE];  // example: 0a05a6b9
    int hflag;                          // 0, 1
};

struct parameters {
    
    int listenfd;
    int threadCount;            // example: 5
    int tflag;                  // 0, 1
    int lflag;                  // 0, 1
    //int hflag;                // 0, 1
    char log_file_name[FILENAME_SIZE];     // example: log_file
    //char log_body_buffer[1000]; // example: 0a05a6b9
};

/*
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

/*
  recv_full()
  Runs recv() repetitively until end of buffer

    ***Citation***
    The design was adapted from the provided code of my previous CSE130 class, as follows:
 
    Title: server.c source code
    Author: Kevin Wang
    Date: 10/19/2021
    Code version: Github
    Availability: https://git.ucsc.edu/cse130/spring20-palvaro/cwang120/-/blob/master/asgn1/httpserver.c
    ***End of Citation***

*/
ssize_t recv_full(int fd, uint8_t *buff, ssize_t size) {
  ssize_t total = 0;
  ssize_t ret = 0;

  // Continues to run recv() until end of buffer
  while (total < size) {
      ret = recv(fd, buff+total, size-total, 0);
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
  return total;
}

/*
  send_full()
  Runs send() repetitively until end of buffer

    ***Citation***
    The design was adapted from the provided code of my previous CSE130 class, as follows:
 
    Title: server.c source code
    Author: Kevin Wang
    Date: 10/19/2021
    Code version: Github
    Availability: https://git.ucsc.edu/cse130/spring20-palvaro/cwang120/-/blob/master/asgn1/httpserver.c
    ***End of Citation***
*/
ssize_t send_full(int fd, uint8_t *buff, ssize_t size, int filedesc) {
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

/*
 * clear_parameters_strings()
 * reset partial data in parameters object
 */

void clear_parameters_strings(struct parameters* specs) {
    //memset(message->log_body_buffer, 0, 1000);
    //specs->hflag = 0;
    memset(specs->log_file_name, 0, FILENAME_SIZE);
}


/*
 * log_request()
 * log request to file when -l is provided
 */
void log_request(struct httpObject* message, struct parameters* specs) {
    char * log_entry = (char*)malloc(sizeof(char)*(LOG_SIZE+1));

    int size = 1000;

    if (message->status_code != 200 && message->status_code != 201) {
        //Example format of a log line for a FAIL request
        //FAIL\tGET /abcd HTTP/1.1\t404\n
        //FAIL\t$(message->method) $(message->filename) HTTP/1.1\t$(message->status_code)\n...

        sprintf(log_entry, "FAIL\t%s %s HTTP/1.1\t%d\n", message->method, message->filename+1, message->status_code);

#if DEBUG == 1
        //printf("Printing my error string for a non-200/non-201 FAIL log: %s\n", log_entry);
#endif
    }
    else {
        char hexstr[2000];
        memset(hexstr, 0, 2000);
        if (strlen(message->log_body_buffer) < 1000) {
            size = strlen(message->log_body_buffer);
        }
        for (int i=0; i < size; i++) {
            sprintf((char*)hexstr + (i*2), "%02hhx", message->log_body_buffer[i]);
        }
        
#if DEBUG == 1
    //printf("message->host = %s\n", message->host);
#endif
        if (strcmp(message->method, "HEAD") != 0) {
            message->content_length = strlen(message->log_body_buffer);
        }
    
        sprintf(log_entry, "%s\t%s\t%s\t%zd\t%s\n", message->method, message->filename + 1, message->host, message->content_length, hexstr);

    }

    int logfiledesc = open(specs->log_file_name, O_CREAT | O_RDWR | O_APPEND , 0600);
    write(logfiledesc, log_entry, strlen(log_entry));
    close(logfiledesc);

    free(log_entry);
    //clear_parameters_strings(specs);
}



/*
 * health_check()
 * Reads log_file and returns number of entries and errors
 */
void health_check(struct httpObject* message, struct parameters* specs) {
    int entryCount = 0;
    int errorCount = 0;
    // tabCount = 0;
    int OFFSET = 0;
    struct stat st;
    //char file_buffer[BUFFER_SIZE];
    char log_buffer[LOG_SIZE];
    char charRead[1];
    char methodRead[5];
    
    int logfiledesc = open(specs->log_file_name, O_RDONLY);
    int logfilespec = stat(specs->log_file_name, &st);
#if DEBUG == 1
    //printf("message->method = %s\n", message->method);
#endif
    if (logfiledesc == -1 || logfilespec == -1) {
        if (errno == EACCES) {
            message->status_code = 403;
        }
        else {
        message->status_code = 404;
        }
    }
    else if (strcmp(message->method, "PUT") == 0 || strcmp(message->method, "HEAD") == 0 ) {
        message->status_code = 403;
    }
    else {
        //a successful health check request
        
        //int INDEX = 0;
        
        //read(logfiledesc, file_buffer, BUFFER_SIZE);
        //while (file_buffer[INDEX] != 0) {
        while (read(logfiledesc, charRead, 1)) {
            if (charRead[0] == '\n') {
                sscanf(log_buffer, "%s", methodRead);

                if (strcmp("FAIL", methodRead) == 0) {
                    errorCount += 1;
                }
                entryCount += 1;
                
                OFFSET = 0;
                memset(log_buffer, 0, LOG_SIZE);
                memset(methodRead, 0, 5);
            }
            else {
                strcpy(&log_buffer[OFFSET],&charRead[0]);
                OFFSET += 1;
            }
            
            //INDEX += 1;
        }
#if DEBUG == 1
    //printf("entry = %d\n", entryCount);
    //printf("error = %d\n", errorCount);
#endif
        memset(message->buffer, 0, BUFFER_SIZE);
        sprintf((char*)message->buffer, "%d\n%d\n", errorCount, entryCount);
        message->content_length = strlen((char*)message->buffer);
        message->status_code = 200;
        
    }
    close(logfiledesc);
    strcpy(message->log_body_buffer, (char*)message->buffer);
    log_request(message, specs);
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

 
/*
* read_http_response()
* Reads request from client and stores information into httpObject message
*/
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
#if DEBUG == 1
          printf("Final length: %d\n", finalLength);
#endif
          int total = 0;
          int offset = readcheck;
          while (cont && total < finalLength) {
            readcheck = recv(connfd, message->buffer+offset+total, BUFFER_SIZE, 0);
            total += readcheck;
            cont = strstr((char *) message->buffer+readcheck, "100-continue\r\n");
          }
        } else if (strcmp(method, "PUT") == 0) {
            //if it's a PUT, it needs to have a content length or its invalid
#if DEBUG == 1
            printf("The request was a bad one due to invalid content length\n");
#endif
            message->status_code = 400;
        }
    }

    free(buffercopy);
    
    if (readcheck == -1) {
      message->status_code = 500;
    }

    return;
}

/*
* process_request()
* Validating request, assigns status code, then performs corresponding task
*/
void process_request(struct httpObject* message, struct parameters* specs) {
 
    char methodRead[6];
    char filenameRead[FILENAME_SIZE];
    char httpversionRead[9];
    char hostRead[FILENAME_SIZE];
    struct stat st;
       
    int contentLengthRead = 0;
    char * clp = NULL;
 
    memset(hostRead, 0, FILENAME_SIZE);
    memset(methodRead, 0, 5);
    memset(filenameRead, 0, FILENAME_SIZE);
    memset(httpversionRead, 0, 9);
       
    /*
    hostRead = strstr((char *) message->buffer, "Host: ");
    
    if (hostRead != NULL) {
     sscanf( (char *)message->buffer, "Host: %s\n", message->host);
     }
     */

#if DEBUG == 1
     //printf("Message->buffer = %s\n", message->buffer);
#endif

    sscanf((char *)message->buffer, "%s %s %s\nHost: %s", methodRead, filenameRead, httpversionRead, hostRead);
    strcpy(message->httpversion, httpversionRead);
    strcpy(message->host, hostRead);
    strcpy(message->method, methodRead);
    strcpy(message->filename, ".");
    strcat(message->filename, filenameRead);

    clp = strstr((char *) message->buffer, "Content-Length: ");
    if (clp != NULL) {
        sscanf(clp, "Content-Length: %d", &contentLengthRead);
        message->content_length = contentLengthRead;
#if DEBUG == 1
        //printf("Content length: %d\n", contentLengthRead);
#endif
     }

#if DEBUG == 1
    //printf("message->host = %s\n", message->host);
    //printf("message->filename = %s\n", message->filename);
#endif
 

    //if (strlen(methodRead) > 6 || strlen(filenameRead) > FILENAME_SIZE || strlen(httpversionRead) > 9 || strcmp(httpversionRead, "HTTP/1.1") != 0) {
     if (is_bad_request(message->filename+2, message->httpversion, message->host)) {
         //pass in message->filename+2 to ignore the first 2 chars "./"
         message->status_code = 400;
     }
     else if (strcmp(methodRead, "GET") != 0 && strcmp(methodRead, "PUT") != 0 && strcmp(methodRead, "HEAD") != 0) {
         message->status_code = 501;
     }
     else if (message->status_code == 400 || message->status_code == 500) {
         return;
     }
     else if (strcmp("/healthcheck", filenameRead) == 0 && message->status_code != 400 && message->status_code != 501) {
#if DEBUG == 1
        //printf("healthcheck() ran\n");
#endif
        //strcpy(message->filename, ".");
        //strcat(message->filename, filenameRead);
     
        health_check(message, specs);
        message->hflag = 1;
    }
    else if (strcmp(methodRead, "GET") == 0 || strcmp(methodRead, "HEAD") == 0) {
      
        strcpy(message->method, methodRead);
        strcpy(message->filename, ".");
        strcat(message->filename, filenameRead);
        
        int filedesc = open(message->filename, O_RDONLY);
        int filespec = stat(message->filename, &st);
        
        if (filedesc == -1 || filespec == -1) {
            if (errno == EACCES) {
                message->status_code = 403;
            }
            else {
            message->status_code = 404;
            }
        }
        else {
            message->content_length = st.st_size;
            message->status_code = 200;
        }
    }
    else if (strcmp(methodRead, "PUT") == 0) {
        //int length = 0;
        char* data;
        //char* clp;
        int filedesc;

        strcpy(message->method, methodRead);
        strcpy(message->filename, ".");
        strcat(message->filename, filenameRead);
        data = strstr((char *) message->buffer, "\r\n\r\n");
        // if file already exists, truncate it
        filedesc = open(message->filename, O_TRUNC | O_RDWR, 0600);
#if DEBUG == 1
        //printf("Truncated an existing file\n");
#endif
        if (filedesc == -1) {
            // otherwise if it doesn't exist, then create it
            filedesc = open(message->filename, O_CREAT | O_RDWR , 0600);
#if DEBUG == 1
            //printf("created new file\n");
#endif
        }

        write(filedesc, data+4, message->content_length);
        //close(filedesc);

        message->status_code = 201;
        
        //log
        pread(filedesc, message->log_body_buffer, message->content_length, 0);

        close(filedesc);
    }
    else {
      message->status_code = 500;
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
void send_http_response(int connfd, struct httpObject* message, struct parameters* specs) {

    send_full(connfd, message->header, message->header_length, -1);

    if (message->hflag == 1 && message->status_code == 200) {
        write(connfd, message->buffer, message->content_length);
    } else if (message->status_code != 200) {
        // a response body for anything other than 200
        send_full(connfd, message->buffer, message->content_length, -1);
    } else if (message->content_length > 0 && strcmp("GET", message->method) == 0) {
        // a successful get request
        int filedesc = open(message->filename, O_RDONLY);
        memset(message->buffer, 0, BUFFER_SIZE);
        send_full(connfd, message->buffer, message->content_length, filedesc);

        if (specs->lflag == 1) {
            pread(filedesc, message->log_body_buffer, message->content_length, 0);
        }

        close(filedesc);
    }
}

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
    memset(message->log_body_buffer, 0, BUFFER_SIZE);
    message->hflag = 0;
}

typedef struct {
    void (*function) (void *);
    void *args;
} threadpool_task_t;

struct threadpool_t {
    pthread_mutex_t lock;
    pthread_mutex_t thread_lock;
    pthread_cond_t task_queue_not_empty;
    pthread_cond_t task_queue_not_full;
    
    //pthread_t dispatcher;
    pthread_t *workers;
    threadpool_task_t * queue;
    
    int idle_thread_count;
    int busy_thread_count;
    int exit_thread_count;
    int thread_min_size;
    int thread_max_size;
    
    int queue_size;
    int head;
    int tail;
    int task_count;
    
    int poolflag;
};

struct dispatcher_args {
    struct threadpool_t *pool;
    struct parameters *specs;
};

struct task_args {
    struct parameters *specs;
    int connfd;
};



/*
* handle_connection()
* To perform the full task in series:
* Read Message -> Process Request -> Create Response -> Send Response
*/
void handle_connection(void * pargs) {

    struct task_args * args = (struct task_args*) pargs;
    struct parameters *specs = args->specs;

    int connfd = args->connfd;
    struct httpObject *message = malloc(sizeof *message);

    clear_httpObject(message);
    
    read_http_response(connfd, message);
    
    process_request(message, specs);

    construct_http_response(message);

    send_http_response(connfd, message, specs);

    if (specs->lflag == 1 && message->hflag != 1) {
        log_request(message, specs);
    }

    free(message);
    close(connfd);
    
}



int is_awake(pthread_t t) {
    int kill = pthread_kill(t, 0);
    if (kill == ESRCH) {
        return 0;
    }
    return 1;
}

void *threadpool_thread (void * tPool) {
    
    struct threadpool_t *pool = (struct threadpool_t *) tPool;
    threadpool_task_t task;

    while(1) {
        pthread_mutex_lock(&(pool->lock));
        while((pool->task_count == 0) && pool->poolflag == 0) {
            pthread_cond_wait(&(pool->task_queue_not_empty), &(pool->lock));
            
            if (pool->exit_thread_count > 0) {
                pool->exit_thread_count -= 1;
                
                if (pool->idle_thread_count > pool->thread_min_size) {
                    pool->idle_thread_count -= 1;
                    pthread_mutex_unlock(&(pool->lock));
                    pthread_exit(NULL);
                }
            }
        }
        
        if (pool->poolflag) {
            pthread_mutex_unlock(&(pool->lock));
            pthread_exit(NULL);
        }
        
        task.function = pool->queue[pool->head].function;
        task.args = pool->queue[pool->head].args;
        
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->task_count -= 1;

        
        pthread_cond_broadcast(&(pool->task_queue_not_full));
        
        pthread_mutex_unlock(&(pool->lock));

        (*(task.function))(task.args);

        pthread_mutex_lock(&(pool->thread_lock));
        pool->busy_thread_count -= 1;
        pthread_mutex_unlock(&(pool->thread_lock));
    }

    pthread_exit(NULL);
    return NULL;
}

int threadpool_free (struct threadpool_t * pool) {
    if (pool == NULL) {
        return -1;
    }
    if (pool->queue != 0) {
        free(pool->queue);
    }
    if (pool->workers != 0) {
        free(pool->workers);
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_mutex_lock(&(pool->thread_lock));
        pthread_mutex_destroy(&(pool->thread_lock));
        pthread_cond_destroy(&(pool->task_queue_not_full));
        pthread_cond_destroy(&(pool->task_queue_not_empty));
    }
    free(pool);
    pool = NULL;
    
    return 0;
}


int threadpool_add(struct threadpool_t *pool, void (*function)(void *), void *args) {
#if DEBUG == 1
    printf("Beginning add()\n");
#endif
    pthread_mutex_lock(&(pool->lock));
        
    while (pool->task_count == pool->queue_size && pool->poolflag == 0) {
        pthread_cond_wait(&(pool->task_queue_not_full), &(pool->lock));
    }
    
    if (pool->poolflag) {
        pthread_mutex_unlock(&(pool->lock));
        return -1;
    }
    
    //printf("pool->tail]: %d \n", pool->tail);
    if (pool->queue[pool->tail].args != NULL) {
        //free(pool->queue[pool->tail].args);
        pool->queue[pool->tail].args = NULL;
    }
    
    //printf("task.args connfd just before adding %d\n", ((struct task_args *)args)->connfd);
    pool->queue[pool->tail].args = args;
    pool->queue[pool->tail].function = function;
    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->task_count += 1;
    //printf("task.args connfd %d\n", ((struct task_args *)(struct threadpool_task_t *)pool->queue[pool->head].args)->connfd);

    pthread_cond_signal(&(pool->task_queue_not_empty));
    pthread_mutex_unlock(&(pool->lock));
#if DEBUG == 1
    printf("Ending add()\n");
#endif
    return 0;
}


//void * dispatcher_function (void *pool_and_specs) {
void * dispatcher_function () {
#if DEBUG == 1
    printf("Running dispatcher()\n");
#endif
    //struct dispatcher_args *d_args = (struct dispatcher_args *) pool_and_specs;
#if DEBUG == 1
    printf("poolflag = %d\n", d_args->pool->poolflag);
#endif
	while(1) {
	sleep(10);
	}

    return NULL;
}


struct threadpool_t * threadpool_create(int tMin, int tMax, int qMax, struct parameters* specs) {    //struct threadpool_t *pool = NULL;

    struct dispatcher_args * pool_and_specs = (struct dispatcher_args *)malloc(sizeof(struct dispatcher_args));
    pool_and_specs->specs = specs;

    do {
        pool_and_specs->pool = (struct threadpool_t *)malloc(sizeof(struct threadpool_t));
        if (pool_and_specs->pool == NULL) {
#if DEBUG == 1
            printf("Pool failed to allocate\n");
#endif
            break;
        }

        pool_and_specs->pool->idle_thread_count = 0;
        pool_and_specs->pool->busy_thread_count = 0;
        pool_and_specs->pool->exit_thread_count = 0;
        pool_and_specs->pool->thread_min_size = tMin;
        pool_and_specs->pool->thread_max_size = tMax;
        pool_and_specs->pool->task_count = 0;
        pool_and_specs->pool->head = 0;
        pool_and_specs->pool->tail = 0;
        pool_and_specs->pool->queue_size = qMax;
        pool_and_specs->pool->poolflag = 0;

        pool_and_specs->pool->workers = (pthread_t *)malloc(sizeof(pthread_t) * tMax);
        if (pool_and_specs->pool->workers == NULL) {
#if DEBUG == 1
            printf("Workers failed to allocate\n");
#endif
            break;
        }
        memset(pool_and_specs->pool->workers, 0, sizeof(pthread_t) * tMax);
        
        pool_and_specs->pool->queue = (threadpool_task_t *)malloc(sizeof(threadpool_task_t) * qMax);
        if (pool_and_specs->pool->queue == NULL) {
#if DEBUG == 1
            printf("Queue failed to allocate\n");
#endif
            break;
        }
        if (pthread_mutex_init(&(pool_and_specs->pool->lock), NULL) != 0 ||
            pthread_mutex_init(&(pool_and_specs->pool->thread_lock), NULL) != 0 ||
            pthread_cond_init(&(pool_and_specs->pool->task_queue_not_empty), NULL) != 0 ||
            pthread_cond_init(&(pool_and_specs->pool->task_queue_not_full), NULL) != 0 )
        {
#if DEBUG == 1
            printf("Locks and Conditions failed to initiate\n");
#endif
            break;
        }

        for (int i=0; i < tMax; i++) {
            pthread_create(&(pool_and_specs->pool->workers[i]), NULL, threadpool_thread, (void*)pool_and_specs->pool);
        }
        //pthread_create(&(pool_and_specs->pool->dispatcher), NULL, dispatcher_function, (void*)pool_and_specs);
        //pthread_create(&(pool_and_specs->pool->dispatcher), NULL, dispatcher_function, (void*)NULL);
        return pool_and_specs->pool;
    } while(0);

    threadpool_free(pool_and_specs->pool);
    return NULL;
}


/*
* main()
* Run and maintain the server to listen for client requests nonstop
*/
int main(int argc, char* argv[]) {
    //int listenfd;
    uint16_t port = 0;

    if (argc < 2) {
        errx(EXIT_FAILURE, "Usage: %s [-N threadCount] [-l log_file_name] port_num\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    
    //struct httpObject message;
    struct parameters * specs = (struct parameters *)malloc(sizeof(struct parameters));
    clear_parameters_strings(specs);
    specs->threadCount = 5;
    specs->tflag = 0;
    specs->lflag = 0;
    specs->listenfd = 0;
    int opt;
    


    
    while ((opt = getopt(argc, argv, "N:l:")) != -1) {
        switch (opt) {
            case 'N':
                specs->tflag = 1;
                specs->threadCount = atoi(optarg);
                break;
            case 'l':
                specs->lflag = 1;
                strcpy(specs->log_file_name, optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-N threadCount] [-l log_file_name] port_num\n", argv[0]);
                exit(EXIT_FAILURE);
            }
    }
    
    if (argv[optind] == NULL) {
        errx(EXIT_FAILURE, "Usage: %s [-N threadCount] [-l log_file_name] port_num\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    else {
        port = strtouint16(argv[optind]);
        if (port == 0) {
            errx(EXIT_FAILURE, "invalid port number: %s", argv[optind]);
            exit(EXIT_FAILURE);
        }
    }
    
    specs->listenfd = create_listen_socket(port);
    //initialize the log file if it doesn't exist yet
    if (specs->lflag == 1) {
        int logfiledesc = open(specs->log_file_name, O_CREAT | O_RDWR | O_APPEND, 0644);
        close(logfiledesc);
    }

    struct threadpool_t *pool = threadpool_create(specs->threadCount, specs->threadCount, QUEUE_SIZE, specs);

    while (1) {
        int connfd;
        while((connfd = accept(specs->listenfd, NULL, NULL))) {
            
            if (connfd < 0) {
                warn("accept error");
                continue;
            }
            
            struct task_args *t_args = (struct task_args *)malloc(sizeof(struct task_args));
            t_args->specs = specs;
            t_args->connfd = connfd;
            if (threadpool_add(pool, handle_connection, (void *)t_args) != 0) {
                return -1;
            }
        }
    }

    return EXIT_SUCCESS;
}
