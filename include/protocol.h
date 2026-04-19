#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdint.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <libgen.h>
#include <math.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

// Header is exactly 3 rows of 32-bits (12 bytes)
#define HEADER_SIZE     12
#define PACKET_SIZE     212992              // Magic UNIX number.
#define SPLIT_SIZE      PACKET_SIZE/4
#define BLINK_SIZE      sizeof(uint16_t)*2

#define FLAG_FIN    0b001
#define FLAG_SYN    0b010
#define FLAG_ACK    0b100

#define TIMEOUT_SEC     2
#define TIMEOUT_USEC    0
#define MAX_RETRIES     5

static const char* MOTION_MSG = ":MotionDetected";
static const size_t MOTION_MSG_LEN = sizeof(":MotionDetected") - 1;

typedef struct{
    uint32_t seq;
    uint32_t ack;
    uint32_t flags;
    uint32_t length; // Internal use only! Not sent over network.
    void* payload;
} Packet;

// Setup.
int SetupServerSocket(char* addr, char* port);
int SetupClientSocket(struct addrinfo* info, char* addr, char* port);

// Packets.
Packet MakePacket(uint32_t seq, uint32_t ack, void* payload, uint32_t length, uint32_t flags);
void PackBlink(void* payload, uint16_t duration, uint16_t times);
void PackMotion(void* payload);
void UnpackBlink(void* payload, uint16_t* durPtr, uint16_t* timesPtr);

// Changed buffer to void* to prevent strict aliasing/pointer math issues
void PacketSerialize(void* buffer, Packet packet);
Packet PacketDeserialize(void* buffer, int recv_len); 

int GetBuffer(struct sockaddr_storage* info, socklen_t* infolen, void* buffer, int sock);
int SendBuffer(struct sockaddr* info, void* buffer, int sock, int size);

// Error checking.
bool CheckRecv(int numbytes, int size);
bool CheckSend(int numbytes, int size);

// Logging.
int LogPacket(char* log, int recv, Packet packet);
int LogFinish(char* log, struct sockaddr_storage* info);
char* Timestamp();
void AddrToChar(char* ipstr, struct sockaddr_in* info);

#endif