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


#define BUFFER_SIZE 6000

// Global string objects for error messages
static const char suc201[] = " 201 Created\r\n\r\n";
static const char err400[] = " 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static const char err403[] = " 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
static const char err404[] = " 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static const char err500[] = " 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
static const char err501[] = " 501 Not Implemented\r\nContent-Length: 0\r\n\r\n";
static const char create[] = "Created\n";

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
    
    char host[256];             // example: 127.0.0.1:1234
    char method[5];             // PUT, HEAD, GET
    char filename[28];          // example: file1.txt
    char httpversion[9];        // HTTP/1.1
    ssize_t content_length;     // example: 13
    ssize_t header_length;      // example: 10
    int status_code;            // example: 404
    uint8_t header[BUFFER_SIZE];
    uint8_t buffer[BUFFER_SIZE];
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
* read_http_response()
* Reads request from client and stores information into httpObject message
*/
void read_http_response(int connfd, struct httpObject* message) {
    
    memset(message->buffer, 0, BUFFER_SIZE);
    int readcheck = recv(connfd, message->buffer, BUFFER_SIZE, 0);
    char* cont = NULL;
    cont = strstr((char *) message->buffer, "100-continue\r\n");
    char* lengthRead = NULL;
    lengthRead = strstr((char*)message->buffer, "Content-Length: ");
    int finalLength;

    if (lengthRead) {
      sscanf(lengthRead, "Content-Length: %d", &finalLength);
      int total = 0;
      int offset = readcheck;
      while (cont && total < finalLength) {
        readcheck = recv(connfd, message->buffer+offset+total, BUFFER_SIZE, 0);
        total += readcheck;
        cont = strstr((char *) message->buffer+readcheck, "100-continue\r\n");
      }
    }
    if (readcheck == -1) {
      message->status_code = 500;
    }

    return;
}

/*
* process_request()
* Validating request, assigns status code, then performs corresponding task
*/
void process_request(struct httpObject* message) {
 
    char methodRead[6];
    char filenameRead[28];
    char httpversionRead[9];
    struct stat st;
    char* hostRead = NULL;
    
    hostRead = strstr((char *) message->buffer, "Host: ");
    
    if (hostRead != NULL) {
        sscanf( (char *)message->buffer, "Host: %s\n", message->host);
    }
    
    sscanf((char *)message->buffer, "%s %s %s", methodRead, filenameRead, httpversionRead);
    strcpy(message->httpversion, httpversionRead);

    if (strlen(methodRead) > 5 || strlen(filenameRead) > 27 || strlen(httpversionRead) > 8 || hostRead == NULL) {
      message->status_code = 400;
    }
    else if (strcmp(methodRead, "GET") != 0 && strcmp(methodRead, "PUT") != 0 && strcmp(methodRead, "HEAD") != 0) {
      message->status_code = 501;
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
        } else {
          message->status_code = 404;
        }
      } else {
         message->content_length = st.st_size;
         message->status_code = 200;
      }
    }
    else if (strcmp(methodRead, "PUT") == 0) {
      int length = 0;
      char* data;
      char* clp;
      int filedesc;

      strcpy(message->method, methodRead);
      strcpy(message->filename, ".");
      strcat(message->filename, filenameRead);
      clp = strstr((char *) message->buffer, "Content-Length: ");
      sscanf(clp, "Content-Length: %d", &length);
      message->content_length = length;
      data = strstr((char *) message->buffer, "\r\n\r\n");
      filedesc = open(message->filename, O_CREAT | O_RDWR, 0600);
      write(filedesc, data+4, length);
      close(filedesc);

      message->status_code = 201;
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

    strcat((char *)message->header, message->httpversion);
    
    if (message->status_code == 200) {
      strcat((char *)message->header, " 200 OK\r\n");
      char lengthStr[28];
      sprintf(lengthStr, "Content-Length: %ld\r\n\r\n", message->content_length);
      strcat((char *)message->header, lengthStr);
    }
    else {     
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
    }
    message->header_length = strlen((char *)message->header);

    return;
}

/*
* send_http_response()
* Delivers status message to client, follow up with content if available
*/
void send_http_response(int connfd, struct httpObject* message) {

    send_full(connfd, message->header, message->header_length, -1);

    if (message->content_length > 0 && strcmp("PUT", message->method) != 0) {
        int filedesc = open(message->filename, O_RDONLY);
        memset(message->buffer, 0, BUFFER_SIZE);
        send_full(connfd, message->buffer, message->content_length, filedesc);
    }
    if (message->status_code == 201) {
        send_full(connfd, (uint8_t *)create, sizeof(create), -1);
    }
}

/*
 * clear_httpObject()
 * reset all data in an httpObject
 */
void clear_httpObject(struct httpObject* message) {
    strcpy(message->method, "");
    strcpy(message->filename, "");
    strcpy(message->httpversion, "");
    message->content_length = 0;
    message->header_length = 0;
    message->status_code = 0;
    strcpy((char *)message->header, "");
    strcpy((char *)message->buffer, "");
}

/*
* handle_connection()
* To perform the full task in series:
* Read Message -> Process Request -> Create Response -> Send Response
*/
void handle_connection(int connfd) {
    struct httpObject message;
    read_http_response(connfd, &message);
    
    process_request(&message);
    
    construct_http_response(&message);

    send_http_response(connfd, &message);
    clear_httpObject(&message);

    // when done, close socket
    close(connfd);
}



/*
* main()
* Run and maintain the server to listen for client requests nonstop
*/
int main(int argc, char *argv[]) {
  int listenfd;
  uint16_t port;

  if (argc != 2) {
    errx(EXIT_FAILURE, "wrong arguments: %s port_num", argv[0]);
  }
  port = strtouint16(argv[1]);
  if (port == 0) {
    errx(EXIT_FAILURE, "invalid port number: %s", argv[1]);
  }
  listenfd = create_listen_socket(port);

  while(1) {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      warn("accept error");
      continue;
    }
      handle_connection(connfd);
  }
  return EXIT_SUCCESS;
}
