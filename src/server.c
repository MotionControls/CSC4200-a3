#include "../include/protocol.h"

int main(int argc, char** argv){
	if(argc < 5) return 1;
	
	// Get args.
	char* port, *logPath;
	for(int i = 1; i < argc; i += 2){
		if(strcmp(argv[i], "-p") == 0){
			port = argv[i+1];
		}else if(strcmp(argv[i], "-s") == 0){
			logPath = argv[i+1];
		}
	}

	if(port == NULL || logPath == NULL){
		printf("Setup err: Missing args.\n");
		return 1;
	}
	
	printf("Starting server...\n\tport = %s\n\tlog = %s\n", port, logPath);

	// Seed rand.
	srand((unsigned)time(NULL) ^ getpid());
	
	int sock = SetupServerSocket("localhost", port);
	
	uint32_t selfIsn = (uint32_t)rand();
	uint32_t theirIsn;
	uint32_t* buffer;
	while(1){
		struct timeval opt = {0,0};
		if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) == -1){
			perror("setsockopt err");
			return errno;
		}
		
		struct sockaddr_storage* theirAddr = malloc(sizeof(struct sockaddr_storage));
		socklen_t theirSize = sizeof(*theirAddr);
		
		// SYN+ACK packets.
		buffer = malloc(PACKET_SIZE);
		int numbytes = GetBuffer(theirAddr, &theirSize, buffer, sock);
		if(CheckSend(numbytes, HEADER_SIZE)) continue;

		Packet synPacket = PacketDeserialize(buffer);
		if(synPacket.flags != FLAG_SYN){
			printf("GetBuffer err: SYN flags incorrect.\n");
			continue;
		}

		theirIsn = synPacket.seq;
		LogPacket(logPath, 1, synPacket);
		printf("Recieved SYN.\nSending ACK...\n");

		Packet synackPacket = MakePacket(selfIsn, synPacket.seq + 1, 0, 0, FLAG_ACK | FLAG_SYN);
		buffer = realloc(buffer, HEADER_SIZE + synackPacket.length);
		PacketSerialize(buffer, synackPacket);

		bool transFail = true;
		int tries = -1;
		while(transFail){
			transFail = false;
			tries++;
			if(tries >= MAX_RETRIES){
				printf("Attempted %i times.\n", tries);
				continue;
			}
			
			opt = (struct timeval){0,0};
			if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) == -1){
				perror("setsockopt err");
				return errno;
			}

			numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE + synackPacket.length);
			if(CheckSend(numbytes, HEADER_SIZE + synackPacket.length)) continue;
			LogPacket(logPath, 0, synackPacket);

			opt = (struct timeval){TIMEOUT_SEC, TIMEOUT_SEC};
			if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) == -1){
				perror("setsockopt err");
				return errno;
			}

			buffer = realloc(buffer, PACKET_SIZE);
			numbytes = GetBuffer(theirAddr, &theirSize, buffer, sock);
			if(CheckRecv(numbytes, HEADER_SIZE)) continue;
			
			Packet ackPacket = PacketDeserialize(buffer);
			if(ackPacket.flags != FLAG_ACK){
				printf("Getbuffer err: ACK flags incorrect.");
				continue;
			}
			LogPacket(logPath, 1, ackPacket);
		};
		if(tries >= MAX_RETRIES) continue;

		printf("Handshake complete.\nWaiting for data...\n");

		// Data packets.
		uint32_t exSeq = theirIsn + 1;
		int packets = 0;
		bool finished = false;
		FILE* download;
		bool doTestInterrupt = false;
		do{
			buffer = realloc(buffer, PACKET_SIZE);
			numbytes = GetBuffer(theirAddr, &theirSize, buffer, sock);
			if(CheckRecv(numbytes, HEADER_SIZE)) continue;

			Packet packet = PacketDeserialize(buffer);
			LogPacket(logPath, 1, packet);
			if(packet.seq != exSeq){
				printf("Packet SEQ not %i.\n", exSeq);
				continue;
			}

			if(doTestInterrupt && packets == 2){
				printf("**********Test Interrupt**********\n");
				doTestInterrupt = false;
				continue;
			}
			
			if(packet.flags == FLAG_FIN){
				printf("Got FIN at packet %i.\n", packets);

				Packet finackPacket = MakePacket(0, packet.seq + 1, 0, 0, FLAG_FIN | FLAG_ACK);
				buffer = realloc(buffer, HEADER_SIZE);
				PacketSerialize(buffer, finackPacket);
				numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE);
				if(CheckSend(numbytes, HEADER_SIZE)) continue;

				LogFinish(logPath, theirAddr);
				finished = true;
			}else{
				uint8_t* writePtr;
				size_t writeSize;
				if(packets == 0){
					// Get filename.
					size_t nameSize = strlen((char*)(packet.payload));
					char* front = "downloads/";
					char nameBuffer[strlen(front) + nameSize];
					strcpy(nameBuffer, front);
					strcat(nameBuffer, (char*)(packet.payload + FILESTR_SIZE));
					printf("Recieving %s to %s\n", (char*)(packet.payload + FILESTR_SIZE), nameBuffer);

					// Open file.
					download = fopen(nameBuffer, "wb");

					// Store payload.
					writePtr = packet.payload + nameSize + 1;
					writeSize = packet.length - nameSize - 1;
				}else{
					writePtr = packet.payload;
					writeSize = packet.length;
				}

				// Send ACK.
				printf("Sending ACK %i.\n", packets);
				Packet ackPacket = MakePacket(0, exSeq + packet.length, 0, 0, FLAG_ACK);
				buffer = realloc(buffer, HEADER_SIZE);
				PacketSerialize(buffer, ackPacket);
				numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE);
				if(CheckSend(numbytes, HEADER_SIZE)) continue;
				LogPacket(logPath, 0, ackPacket);

				fwrite(writePtr, sizeof(uint8_t), writeSize, download);
				exSeq += packet.length;
				packets++;
			}
		}while(!finished);
		fclose(download);

		printf("Waiting for next client...\n");
		selfIsn = (uint32_t)rand();
	}

	printf("Exiting...\n");
	return 0;
}