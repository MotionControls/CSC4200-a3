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
	uint32_t* buffer = malloc(PACKET_SIZE);
	while(1){
		uint32_t selfIsn = (uint32_t)rand();
		int curSeq;
		uint32_t theirIsn;
		uint16_t blinks = 0;
		uint16_t duration = 0;
			
		struct timeval timeOpt = {0,0};
		if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
			perror("setsockopt err");
			return errno;
		}
		
		struct sockaddr_storage* theirAddr = malloc(sizeof(struct sockaddr_storage));
		socklen_t theirSize = sizeof(*theirAddr);
		
		// SYN+ACK packets.
		buffer = realloc(buffer, PACKET_SIZE);
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

		Packet synackPacket = MakePacket(selfIsn, theirIsn + 1, 0, 0, FLAG_ACK | FLAG_SYN);
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
		curSeq = theirIsn + 1;

		printf("Handshake complete.\n");

		// Get blink params.
		buffer = realloc(buffer, HEADER_SIZE + BLINK_SIZE);
		numbytes = GetBuffer(theirAddr, &theirSize, buffer, sock);
		if(CheckRecv(numbytes, HEADER_SIZE + BLINK_SIZE)) continue;
		curSeq += numbytes;

		Packet blinkPacket = PacketDeserialize(buffer);
		LogPacket(logPath, 1, blinkPacket);
		UnpackBlink(blinkPacket.payload, &duration, &blinks);
		printf("Blink params set: %u blinks for %ums\n.", blinks, duration);

		// ACK blink params.
		buffer = realloc(buffer, HEADER_SIZE + BLINK_SIZE);
		Packet blinkAckPacket = blinkPacket;
		blinkAckPacket.flags = FLAG_ACK;
		blinkAckPacket.ack = curSeq;
		PacketSerialize(buffer, blinkAckPacket);
		numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE + BLINK_SIZE);
		if(CheckSend(numbytes, HEADER_SIZE + BLINK_SIZE)) continue;
		LogPacket(logPath, 0, blinkAckPacket);

		// Wait for motion.
		buffer = realloc(buffer, HEADER_SIZE + MOTION_MSG_LEN);
		numbytes = GetBuffer(theirAddr, &theirSize, buffer, sock);
		if(CheckRecv(numbytes, HEADER_SIZE + MOTION_MSG_LEN)) continue;
		curSeq += numbytes;

		Packet packet = PacketDeserialize(buffer);
		LogPacket(logPath, 1, packet);
		char motionRecv[MOTION_MSG_LEN];
		memcpy(motionRecv, (char*)packet.payload, MOTION_MSG_LEN);	// We love band-aids.
		if(strcmp(motionRecv, MOTION_MSG) != 0){
			printf("payload err: Expected \"%s\", got \"%s\".\n", MOTION_MSG, (char*)packet.payload);
			continue;
		}
		
		/*
			Blink mf.
		*/
		if(strcmp(motionRecv, MOTION_MSG) == 0){
			for (int ii = 0 ; ii < blinks; ii++) {
			GPIO.output(17, GPIO.HIGH) # LED ON (3.3 V)
			sleep(duration);
			GPIO.output(17, GPIO.LOW); # LED OFF (0 V)
			sleep(duration);
			}
			}


		// Send ACK.
		printf("Sending ACK.\n");
		Packet ackPacket = MakePacket(selfIsn, curSeq, 0, 0, FLAG_ACK);
		buffer = realloc(buffer, HEADER_SIZE);
		PacketSerialize(buffer, ackPacket);
		numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE);
		if(CheckSend(numbytes, HEADER_SIZE)) continue;
		LogPacket(logPath, 0, ackPacket);
		curSeq++;

		// Get FIN.
		buffer = realloc(buffer, HEADER_SIZE);
		numbytes = GetBuffer(theirAddr, &theirSize, buffer, sock);
		if(CheckRecv(numbytes, HEADER_SIZE)) continue;

		Packet finPacket = PacketDeserialize(buffer);
		LogPacket(logPath, 1, finPacket);
		if(finPacket.flags != FLAG_FIN){
			printf("Incorrect FIN.\n");
			continue;
		}

		// Send FIN+ACK.
		Packet finackPacket = MakePacket(0, curSeq, 0, 0, FLAG_FIN | FLAG_ACK);
		buffer = realloc(buffer, HEADER_SIZE);
		PacketSerialize(buffer, finackPacket);
		numbytes = SendBuffer((struct sockaddr*)theirAddr, buffer, sock, HEADER_SIZE);
		if(CheckSend(numbytes, HEADER_SIZE)) continue;

		LogFinish(logPath, theirAddr);
		printf("Waiting for next client...\n");
	}

	printf("Exiting...\n");
	return 0;
}