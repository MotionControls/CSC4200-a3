#ifndef SHARED_H
#define SHARED_H

#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "protocol.h"

#define BUFFER_SIZE	100
#define TIMEOUT		10

void AddrToChar(char* ipstr, struct addrinfo* info){
	void* addr;
	struct sockaddr* check = (struct sockaddr*)info->ai_addr;
	if(check->sa_family == AF_INET){
		addr = &(((struct sockaddr_in*)check)->sin_addr);
	}else{
		addr = &(((struct sockaddr_in6*)check)->sin6_addr);
	}
	inet_ntop(info->ai_family, addr, ipstr, sizeof(ipstr));
}

int CreateSocket(struct addrinfo* res){
	printf("Creating socket...\n");
	int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if(sock == -1){
		perror("socket err");
		exit(1);
	}
	
	return sock;
}

/*	CheckSend(numbytes, size);
	Returns true if error, otherwise false.
*/
bool CheckSend(int numbytes, int size){
	if(numbytes < size){
		perror("send err");
		printf("Sent %u bytes.\n", numbytes);
		return 1;
	}
	
	return 0;
}

/*	CheckRecv(numbytes, size, startTime);
	Returns true if error, otherwise false.
*/
bool CheckRecv(int numbytes, int size, time_t startTime){
	if(numbytes < size){
		perror("recv err");
		printf("Received %u bytes.\n", numbytes);
		return true;
	}
	
	if(time(NULL) - startTime > TIMEOUT){
		printf("recv err: Timeout.\nReceived %i bytes.\n", numbytes);
		return true;
	}
	
	return false;
}

#endif