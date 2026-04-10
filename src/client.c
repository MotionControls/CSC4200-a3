#include "../include/protocol.h"

int main(int argc, char** argv){
	if(argc < 7) return 1;
	
	// Get args.
	char* port, *logPath, *serverIp;
	for(int i = 1; i < argc; i += 2){
		if(strcmp(argv[i], "-p") == 0){
			port = argv[i+1];
		}else if(strcmp(argv[i], "-l") == 0){
			logPath = argv[i+1];
		}else if(strcmp(argv[i], "-s") == 0){
			serverIp = argv[i+1];
		}
	}

	if(port == NULL || serverIp == NULL || logPath == NULL){
		printf("Setup err: Missing args.\n");
		return 1;
	}
	
	printf("Starting client...\n\tport = %s\n\tIP = %s\n\tlog = %s\n", port, serverIp, logPath);
	
	// Seed rand.
	srand((unsigned)time(NULL) ^ getpid());

	struct addrinfo* theirAddr = malloc(sizeof(struct addrinfo));
	socklen_t theirSize = sizeof(*theirAddr);
	
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	int code = getaddrinfo(serverIp, port, &hints, &theirAddr);
	if(code != 0){
		printf("getaddrinfo err: %s\n", gai_strerror(code));
		return 1;
	}

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1){
		perror("socket err");
		return errno;
	}

	char foundAddr[INET_ADDRSTRLEN];
	printf("Socket setup for %s.\n", inet_ntop(theirAddr->ai_family, &(((struct sockaddr_in*)(theirAddr->ai_addr))->sin_addr), foundAddr, INET_ADDRSTRLEN));
	
	// SYN+ACK packets.
	struct timeval opt = {TIMEOUT_SEC, TIMEOUT_SEC};
	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) == -1){
		perror("setsockopt err");
		return errno;
	}

	uint32_t selfIsn = (uint32_t)rand();
	uint32_t curSeq;

	Packet synackPacket;
	uint32_t* buffer;
	bool transFail;
	int tries = -1;
	do{
		transFail = false;
		tries++;
		if(tries >= MAX_RETRIES){
			printf("Attempted %i times.\n", tries);
			close(sock);
			return 1;
		}
		
		printf("Sending SYN...\n");
		Packet synPacket = MakePacket(selfIsn, 0, 0, 0, FLAG_SYN);
		buffer = malloc(HEADER_SIZE);
		PacketSerialize(buffer, synPacket);
		int numbytes = SendBuffer((struct sockaddr*)theirAddr->ai_addr, buffer, sock, HEADER_SIZE);
		if(CheckSend(numbytes, HEADER_SIZE)){transFail = true; continue;}
		LogPacket(logPath, 0, synPacket);

		buffer = realloc(buffer, HEADER_SIZE);
		numbytes = GetBuffer((struct sockaddr*)theirAddr->ai_addr, &theirSize, buffer, sock);
		if(CheckRecv(numbytes, HEADER_SIZE)){transFail = true; continue;}
		printf("Recieved ACK.\n");
		
		synackPacket = PacketDeserialize(buffer);
		LogPacket(logPath, 1, synackPacket);
		if(synackPacket.flags != (FLAG_SYN | FLAG_ACK) || synackPacket.ack != selfIsn+1){
			printf("GetBuffer err: ACK flags or ACK num incorrect.\n");
			transFail = true;
			continue;
		}

		curSeq = selfIsn + 1;
	}while(transFail);

	printf("Sending ACK...\n");
	Packet ackPacket = MakePacket(synackPacket.seq, curSeq, 0, 0, FLAG_ACK);
	buffer = realloc(buffer, HEADER_SIZE);
	PacketSerialize(buffer, ackPacket);
	int numbytes = SendBuffer((struct sockaddr*)theirAddr->ai_addr, buffer, sock, HEADER_SIZE);
	if(CheckSend(numbytes, HEADER_SIZE)) return errno;
	LogPacket(logPath, 0, ackPacket);

	printf("Handshake complete.\n");

	/*
		Loop send data packets here.
	*/

	// Send FIN.
	printf("Sending FIN...\n");
	transFail = true;
	tries = -1;
	do{
		tries++;
		Packet finPacket = MakePacket(curSeq, 0, 0, 0, FLAG_FIN);
		buffer = realloc(buffer, HEADER_SIZE);
		PacketSerialize(buffer, finPacket);
		numbytes = SendBuffer((struct sockaddr*)theirAddr->ai_addr, buffer, sock, HEADER_SIZE);
		if(CheckSend(numbytes, HEADER_SIZE)) continue;
		LogPacket(logPath, 0, finPacket);

		numbytes = GetBuffer((struct sockaddr*)theirAddr->ai_addr, &theirSize, buffer, sock);
		if(CheckRecv(numbytes, HEADER_SIZE)) continue;
		Packet finackPacket = PacketDeserialize(buffer);
		if(finackPacket.flags != (FLAG_ACK | FLAG_FIN) || finackPacket.ack != curSeq + 1) continue;
		LogPacket(logPath, 1, finackPacket);
		
		printf("Connection closed cleanly.\n");
		transFail = false;
	}while(transFail && tries < 5);

	close(sock);
	printf("Exiting...\n");
	return 0;
}