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

int main(int argc, char* argv[]) {

	int optN = 5;
	int optR = 5;
	int opts = 3;
	int optm = 1024;
	int port = 0;
	int server_count = argc - 2;

	int opt;
	//opt = getopt(argc, argv, "N:R:s:m:")) != -1;
	while ((opt = getopt(argc, argv, "N:R:s:m:")) != -1) {
        switch (opt) {
            case 'N':
                if (is_positive(optarg) != 1 ) {
                	errx(EXIT_FAILURE, "invalid number of parallel connections: -N (%s)", optarg);
            		exit(EXIT_FAILURE);
                }
                else {
					optN = atoi(optarg);
                	server_count = server_count - 2;
                }
                break;
            case 'R':
            	if (is_positive(optarg) != 1 ) {
                	errx(EXIT_FAILURE, "invalid number of healthcheck frequency: -R (%s)", optarg);
            		exit(EXIT_FAILURE);
                }
                else {
                	optR = atoi(optarg);
                	server_count = server_count - 2;
            	}
                break;
            case 's':
            	if (is_nonnegative(optarg) != 1 ) {
                	errx(EXIT_FAILURE, "invalid cache capacity: -s (%s)", optarg);
            		exit(EXIT_FAILURE);
                }
                else {
                	opts = atoi(optarg);
                	server_count = server_count - 2;
            	}
                break;
            case 'm':
            	if (is_nonnegative(optarg) != 1 ) {
                	errx(EXIT_FAILURE, "invalid maximum cach file size: -m (%s)", optarg);
            		exit(EXIT_FAILURE);
                }
                else {
                	optm = atoi(optarg);
                	server_count = server_count - 2;
            	}
                break;
            default:
                fprintf(stderr, "Usage: %s port [-N connections] [-R rate_of_healthcheck] [-s cache_capacity] [-m max_cache_size] servers...\n", argv[0]);
                exit(EXIT_FAILURE);
            }
    }
    if (server_count == 0) {
    	errx(EXIT_FAILURE, "Missing server ports");
        exit(EXIT_FAILURE);
    }
    //else if (argv[1] == NULL) {
    else if (server_count < 0) {
        errx(EXIT_FAILURE, "Usage: %s port [-N connections] [-R rate_of_healthcheck] [-s cache_capacity] [-m max_cache_size] servers...\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    else {
        if (is_valid_port(argv[optind]) != 1) {								//optind => 1
            errx(EXIT_FAILURE, "invalid port number: %s", argv[optind]);	//optind => 1
            exit(EXIT_FAILURE);
        }
        else {
        	port = strtouint16(argv[optind]);								//optind =>1
        }
    }
    int servers[server_count];
    int PORT_INDEX = 0;
	
	if (optind < argc) {
        while (optind < argc) {
        	if(is_valid_port(argv[optind]) != 1) {								
        		errx(EXIT_FAILURE, "invalid port number: %s", argv[optind]);
           		exit(EXIT_FAILURE);
        	}
        	else {
				if (PORT_INDEX != 0) {
            		servers[PORT_INDEX-1] = atoi(argv[optind]);
	            }
            	PORT_INDEX++;
            	optind++;
        	}
        }
    }


    printf("---Final result:---\n");
    printf("Port: %d\n", port);
	printf("-N: %d\n", optN);
	printf("-R: %d\n", optR);
	printf("-s: %d\n", opts);
	printf("-m: %d\n", optm);
	printf("---\n");
	printf("argc: %d\n", argc);
	
    for (PORT_INDEX = 0; PORT_INDEX < server_count; PORT_INDEX++) {
    	printf("server port %d:\t%d\n", PORT_INDEX+1, servers[PORT_INDEX]);
    }
		
	printf("---\n");
	return 0;
}