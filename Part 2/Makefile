#------------------------------------------------------------------------------
# Makefile for Assignment 2
#
# make                   makes httpserver
# make clean             cleans out all binaries created from make
#------------------------------------------------------------------------------

httpserver : httpserver.c
	gcc -Wall -Wextra -Wpedantic -Wshadow -lpthread -pthread -o httpserver httpserver.c

clean :
	rm -f httpserver
