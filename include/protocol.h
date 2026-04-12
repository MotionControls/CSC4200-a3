#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
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

/*
 * protocol.h
 * CSC4200 — Program 2: TCP-Like Reliable Protocol over UDP
 *
 * This header defines the packet structure and constants for the
 * custom reliability protocol you will implement.
 *
 * DO NOT change field names, sizes, or the HEADER_SIZE constant.
 * Your serialization and deserialization must match this layout exactly.
 *
 * Packet Wire Format (all fields big-endian / network byte order):
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Sequence Number  (32 bits)                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Acknowledgment Number (32 bits)              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                   Not Used (29 bits)                    |A|S|F|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Payload Length (32 bits)                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Payload (variable)                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Flag bits (low 3 bits of the flags field):
 *   Bit 0 (F) — FIN  : No more data from sender; initiate teardown
 *   Bit 1 (S) — SYN  : Synchronize sequence numbers (handshake)
 *   Bit 2 (A) — ACK  : Acknowledgment Number field is valid
 */

#define HEADER_SIZE		sizeof(uint32_t)*4
#define PACKET_SIZE		212992				// Magic UNIX number.
#define SPLIT_SIZE		PACKET_SIZE/4

#define FLAG_FIN	0b001
#define FLAG_SYN	0b010
#define FLAG_ACK	0b100

#define TIMEOUT_SEC		2
#define TIMEOUT_USEC	0
#define MAX_RETRIES		5

typedef struct{
	uint32_t seq;
	uint32_t ack;
	uint32_t flags;
	uint32_t length;
	void* payload;
}Packet;

// Setup.
int SetupServerSocket(char* addr, char* port);
int SetupClientSocket(struct addrinfo* info, char* addr, char* port);

// Packets.
Packet MakePacket(uint32_t seq, uint32_t ack, void* payload, uint32_t length, uint32_t flags);
void PackBlink(void* payload, uint16_t duration, uint16_t times);
void PackMotion(void* payload);
void UnpackBlink(void* payload, uint16_t* durPtr, uint16_t* timesPtr);
void PacketSerialize(uint32_t* buffer, Packet packet);
Packet PacketDeserialize(uint32_t* buffer);
Packet HeaderDeserialize(uint32_t* buffer);
int GetBuffer(struct sockaddr_storage* info, socklen_t* infolen, uint32_t* buffer, int sock);
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