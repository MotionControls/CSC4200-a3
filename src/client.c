#include "../include/protocol.h"

int main(int argc, char** argv){
	// Get args.
	char* port, *logPath, *serverIp;
	
	bool havePort = false;
	bool haveLog = false;
	bool haveIP = false;
	int opt;
	while((opt = getopt(argc, argv, "p:l:s:")) != -1){
		switch(opt){
			case 'p':
				port = optarg;
				havePort = true;
				break;
			case 'l':
				logPath = optarg;
				haveLog = true;
				break;
			case 's':
				serverIp = optarg;
				haveIP = true;
				break;
			default:
				printf("Unknown arg %c.\n", opt);
				return 1;
		}
	}

	if(!havePort || !haveLog || !haveIP){
		printf("Missing args.\n");
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
	struct timeval timeOpt = {TIMEOUT_SEC, TIMEOUT_SEC};
	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
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

	// Send blink params.
	void* blinkParams = malloc(BLINK_SIZE);
	uint16_t duration = 1000;
	uint16_t times = 5;
	PackBlink(blinkParams, duration, times);
	
	Packet blinkPacket = MakePacket(curSeq, 0, blinkParams, BLINK_SIZE, 0);
	buffer = realloc(buffer, HEADER_SIZE + BLINK_SIZE);
	PacketSerialize(buffer, blinkPacket);
	numbytes = SendBuffer((struct sockaddr*)theirAddr->ai_addr, buffer, sock, HEADER_SIZE + BLINK_SIZE);
	if(CheckSend(numbytes, HEADER_SIZE + BLINK_SIZE)) return errno;
	LogPacket(logPath, 0, blinkPacket);
	curSeq += numbytes;

	// Get ACK.
	buffer = realloc(buffer, HEADER_SIZE + BLINK_SIZE);
	numbytes = GetBuffer((struct sockaddr*)theirAddr->ai_addr, &theirSize, buffer, sock);
	if(CheckRecv(numbytes, HEADER_SIZE + BLINK_SIZE)) return errno;

	Packet blinkAckPacket = PacketDeserialize(buffer);
	if(blinkAckPacket.flags != FLAG_ACK || blinkAckPacket.ack != curSeq){
		printf("recv err: Expected SEQ %i, got %i.", curSeq, blinkAckPacket.ack);
		return 1;
	}
	LogPacket(logPath, 1, blinkAckPacket);

	uint16_t recvDur, recvTimes;
	UnpackBlink(blinkAckPacket.payload, &recvDur, &recvTimes);
	if(recvDur != duration || recvTimes != times){
		printf("UnpackBlink err: Params do not match sent.\n");
		return 1;
	}

	printf("Waiting for motion...\n");
	/* Block for motion. */
	// TEMP Test.
	Packet motionPacket = MakePacket(curSeq, 0, MOTION_MSG, MOTION_MSG_LEN, 0);
	buffer = realloc(buffer, HEADER_SIZE + MOTION_MSG_LEN);
	PacketSerialize(buffer, motionPacket);
	numbytes = SendBuffer((struct sockaddr*)theirAddr->ai_addr, buffer, sock, HEADER_SIZE + MOTION_MSG_LEN);
	if(CheckSend(numbytes, HEADER_SIZE + MOTION_MSG_LEN)) return errno;
	LogPacket(logPath, 0, motionPacket);
	curSeq += numbytes;

	// Recv. motion FIN.
	// TODO

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
		if(finackPacket.flags != (FLAG_ACK | FLAG_FIN) || finackPacket.ack != curSeq + 1){
			printf("FIN+ACK flags or SEQ incorrect. Expected ACK %i, got %i.\n", curSeq + 1, finackPacket.ack);
			continue;
		}
		LogPacket(logPath, 1, finackPacket);
		
		printf("Connection closed cleanly.\n");
		transFail = false;
	}while(transFail && tries < 5);

	close(sock);
	printf("Exiting...\n");
	return 0;
}