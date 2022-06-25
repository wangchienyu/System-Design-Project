#------------------------------------------------------------------------------
# Makefile for Assignment 2
#
# make                   makes httpsproxy
# make clean             cleans out all binaries created from make
#------------------------------------------------------------------------------

httpproxy : httpproxy.c
	gcc -Wall -Wextra -Wpedantic -Wshadow -lpthread -pthread -o httpproxy httpproxy.c

clean :
	rm -f httpproxy
