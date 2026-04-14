#include "../include/protocol.h"

int main(int argc, char** argv){
	// Get args.
	// https://man7.org/linux/man-pages/man3/getopt.3.html
	char* port, *logPath;

	bool havePort = false;
	bool haveLog = false;
	int opt;
	while((opt = getopt(argc, argv, "p:s:")) != -1){
		switch(opt){
			case 'p':
				port = optarg;
				int temp = atoi(port);
				if(temp < 1 || temp > 65535){
					printf("Port must be between 1 and 65535.\n");
					return 1;
				}
				havePort = true;
				break;
			case 's':
				logPath = optarg;
				haveLog = true;
				break;
			default:
				printf("Unknown arg %c.\n", opt);
				return 1;
		}
	}

	if(!havePort || !haveLog){
		printf("Missing args.\n");
		return 1;
	}
	
	printf("Starting server...\n\tport = %s\n\tlog = %s\n", port, logPath);

	// Seed rand.
	srand((unsigned)time(NULL) ^ getpid());
	
	int sock = SetupServerSocket("localhost", port);
	
	uint32_t selfIsn = (uint32_t)rand();
	uint32_t theirIsn;
	uint32_t* buffer;
	uint16_t blinks = 0;
	uint16_t duration = 0;
	while(1){
		struct timeval timeOpt = {0,0};
		if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
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
			
			timeOpt = (struct timeval){0,0};
			if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
				perror("setsockopt err");
				return errno;
			}

			numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE + synackPacket.length);
			if(CheckSend(numbytes, HEADER_SIZE + synackPacket.length)) continue;
			LogPacket(logPath, 0, synackPacket);

			timeOpt = (struct timeval){TIMEOUT_SEC, TIMEOUT_SEC};
			if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
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

		printf("Handshake complete.\n");

		// Get blink params.
		buffer = realloc(buffer, HEADER_SIZE + BLINK_SIZE);
		numbytes = GetBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE + BLINK_SIZE);
		if(CheckRecv(numbytes, HEADER_SIZE + BLINK_SIZE)) continue;

		Packet blinkPacket = PacketDeserialize(buffer);
		LogPacket(logPath, 1, blinkPacket);
		UnpackBlink(blinkPacket.payload, &duration, &blinks);
		printf("Blink params set: %u blinks for %ums.", blinks, duration);

		// ACK blink params.
		buffer = realloc(buffer, HEADER_SIZE + BLINK_SIZE);
		Packet blinkAckPacket = blinkPacket;
		blinkAckPacket.flags = FLAG_ACK;
		PacketSerialize(buffer, blinkAckPacket);
		numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE + BLINK_SIZE);
		if(CheckSend(numbytes, HEADER_SIZE + BLINK_SIZE)) continue;

		// Data packets.
		uint32_t exSeq = theirIsn + 1;
		int packets = 0;
		bool finished = false;
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
				/*
					Do stuff with data packet here.
				*/

				// Send ACK.
				printf("Sending ACK %i.\n", packets);
				Packet ackPacket = MakePacket(0, exSeq + packet.length, 0, 0, FLAG_ACK);
				buffer = realloc(buffer, HEADER_SIZE);
				PacketSerialize(buffer, ackPacket);
				numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE);
				if(CheckSend(numbytes, HEADER_SIZE)) continue;
				LogPacket(logPath, 0, ackPacket);

				exSeq += packet.length;
				packets++;
			}
		}while(!finished);

		printf("Waiting for next client...\n");
		selfIsn = (uint32_t)rand();
	}

	printf("Exiting...\n");
	return 0;
}