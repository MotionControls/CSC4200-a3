#include "protocol.h"

/*	SetupServerSocket(addr, port)
	Setups a server socket.
	Returns socket.
*/
int SetupServerSocket(char* addr, char* port){
	// Setup addr.
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if(addr == NULL) hints.ai_flags = AI_PASSIVE;

	struct addrinfo* info;
	int code = getaddrinfo(addr, port, &hints, &info);
	if(code != 0){
		printf("getaddrinfo err: %s\n", gai_strerror(code));
		exit(1);
	}

	// Loop through possible bindings.
	int sock;
	struct addrinfo* cur;
	char foundAddr[INET_ADDRSTRLEN];
	for(cur = info; cur != NULL; cur = cur->ai_next){
		printf("\tTesting address %s.\n", inet_ntop(cur->ai_family, &(((struct sockaddr_in*)(cur->ai_addr))->sin_addr), foundAddr, INET_ADDRSTRLEN));
		
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if(sock == -1){
			perror("socket err");
			continue;
		}

		int reuse = 1;
		if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &reuse, sizeof(int)) < 0){
			perror("setsockopt err");
			continue;
		}

		if(bind(sock, cur->ai_addr, cur->ai_addrlen) == -1){
			close(sock);
			perror("bind err");
			continue;
		}

		break;
	}

	if(cur == NULL){
		printf("Failed to bind socket.\n");
		exit(1);
	}
	
	printf("Socket setup for %s:%s.\n", inet_ntop(cur->ai_family, &(((struct sockaddr_in*)(cur->ai_addr))->sin_addr), foundAddr, INET_ADDRSTRLEN), port);

	// Here for localhost debugging.
	if(strcmp(addr, "localhost") == 0 || strcmp(addr, "127.0.0.1") == 0){
		FILE* file;
		file = fopen("addr", "w");
		fprintf(file, foundAddr);
		fclose(file);
	}
	
	return sock;
}

/*	MakePacket(seq, ack, payload, length, flags)
	Returns a packet struct.
*/
Packet MakePacket(uint32_t seq, uint32_t ack, void* payload, uint32_t length, uint32_t flags){
	Packet packet;
	packet.seq = seq;
	packet.ack = ack;
	packet.flags = flags;
	packet.length = length;
	packet.payload = payload;

	return packet;
}

/*	PacketSerialize(packet);
	Morphs packet into byte array.
*/
void PacketSerialize(uint32_t* buffer, Packet packet){
	uint32_t seq = htonl(packet.seq);
	uint32_t ack = htonl(packet.ack);
	uint32_t flags = htonl(packet.flags);
	uint32_t length = htonl(packet.length);

	memcpy(&buffer[0], &seq, sizeof(uint32_t));
	memcpy(&buffer[1], &ack, sizeof(uint32_t));
	memcpy(&buffer[2], &flags, sizeof(uint32_t));
	memcpy(&buffer[3], &length, sizeof(uint32_t));
	if(length > 0) memcpy(&buffer[4], packet.payload, packet.length);
}

/*	PacketDeserialize(buffer)
	Morphs buffer into packet struct.
	Returns packet struct.
*/
Packet PacketDeserialize(uint32_t* buffer){
	uint32_t seq = ntohl(buffer[0]);
	uint32_t ack = ntohl(buffer[1]);
	uint32_t flags = ntohl(buffer[2]);
	uint32_t length = ntohl(buffer[3]);
	
	if(length <= 0) return MakePacket(seq, ack, 0, 0, flags);
	
	void* ptr = malloc(length);
	memcpy(ptr, &(buffer[4]), length);
	
	return MakePacket(seq, ack, ptr, length, flags);
}

/*	HeaderDeserialize(buffer)
	Gets header from buffer for reading length.
	Should probably just make a function for length.
	Returns packet struct.
*/
Packet HeaderDeserialize(uint32_t* buffer){
	uint32_t seq = ntohl(buffer[0]);
	uint32_t ack = ntohl(buffer[1]);
	uint32_t flags = ntohl(buffer[2]);
	uint32_t length = ntohl(buffer[3]);
	uint32_t payload = 0;

	return MakePacket(seq, ack, &payload, length, flags);
}

/*	LogPacket(log, recv, packet)
	Logs a packet in the following format:
		[YYYY-MM-DD-HH-MM-SS] SEND|RECV  SEQ=<n> ACK=<n> [ACK] [SYN] [FIN] [LEN=<n>]
	Returns 1 on success, 0 otherwise.
log 	;	Logfile path.
recv	;	Assumes RECV if >0, SEND otherwise.
*/
int LogPacket(char* log, int recv, Packet packet){
	FILE* file;
	file = fopen(log, "a");
	fprintf(file, "%s %s SEQ=%u ACK=%u %u %u %u LEN=%u\n",
		Timestamp(),
		(recv) ? "RECV" : "SEND",
		packet.seq,
		packet.ack,
		(packet.flags & FLAG_ACK) >> 2,	// If unshifted, number shows up as >1 when set.
		(packet.flags & FLAG_SYN) >> 1,
		packet.flags & FLAG_FIN,
		packet.length);
	fflush(file);
	fclose(file);

	return 1;
}

int LogFinish(char* log, struct sockaddr_storage* info){
	FILE* file;
	file = fopen(log, "a");
	char addr[INET_ADDRSTRLEN];
	fprintf(file, ":Interaction with %s completed.\n", inet_ntop(info->ss_family, &(((struct sockaddr_in*)info)->sin_addr), addr, INET_ADDRSTRLEN));
	fflush(file);
	fclose(file);

	return 1;
}

/*	Timestamp()
	Returns a the current time in the format:
		YYYY-MM-DD-HH-MM-SS
*/
char* Timestamp(){
	char* buffer = malloc(50);
	time_t now = time(NULL);
	strftime(buffer, 50, "%Y-%m-%d-%H-%M-%S", localtime(&now));
	return buffer;
}

/*	GetBuffer(info, infolen, buffer, sock)
	Gets a buffer from info.
	Returns number of bytes received.
*/
int GetBuffer(struct sockaddr_storage* info, socklen_t* infolen, uint32_t* buffer, int sock){
	int numbytes = 0;
	int size = PACKET_SIZE;
	bool printedAddr = false;
	do{
		int got = recvfrom(sock, (buffer + numbytes), size - numbytes, 0, (struct sockaddr*)info, infolen);
		if(got == -1){
			perror("recvfrom err");
			if(errno == EFAULT)	exit(errno);
			return -1;
		}

		if(!printedAddr){
			char foundAddr[INET_ADDRSTRLEN];
			printf("recvfrom %s.\n", inet_ntop(info->ss_family, &(((struct sockaddr_in*)info)->sin_addr), foundAddr, INET_ADDRSTRLEN));
			printedAddr = true;
		}

		numbytes += got;
		printf("\t%i / %i\n", numbytes, size);

		if(numbytes >= HEADER_SIZE && size == PACKET_SIZE){
			size = ntohl(buffer[3]) + HEADER_SIZE;
			printf("\tNew size: %i.\n", size);
		}
	}while(numbytes < size);
	
	printf("Fin\t%i / %i\n", numbytes, size);
	return numbytes;
}

/*	SendBuffer(info, buffer, sock, size)
	Sends a buffer to info.
	Returns number of bytes sent.
*/
int SendBuffer(struct sockaddr* info, void* buffer, int sock, int size){
	int numbytes = 0;
	do{
		int sent = sendto(sock, buffer + numbytes, size, 0, info, sizeof(*info));
		if(sent == -1){
			perror("sendto err");
			return -1;
		}

		numbytes += sent;
		printf("\t%i / %i\n", numbytes, size);
	}while(numbytes < size);

	printf("\tSent %i bytes.\n", numbytes);
	return numbytes;
}

/*	CheckRecv(numbytes, size)
	Checks numbytes against size.
	Could probably be combined with CheckSend.
	Returns true if error, false otherwise.
*/
bool CheckRecv(int numbytes, int size){
	if(numbytes < size){
		printf("\tReceived %u bytes.\n", numbytes);
		return true;
	}
	
	return false;
}

/*	CheckSend(numbytes, size)
	Checks numbytes against size.
	Could probably be combined with CheckRecv.
	Returns true if error, false otherwise.
*/
bool CheckSend(int numbytes, int size){
	if(numbytes < size){
		printf("\tSent %u bytes.\n", numbytes);
		return true;
	}
	
	return false;
}