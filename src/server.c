#include "../include/protocol.h"
#include <wiringPi.h>

// WiringPi Pin 0 maps to Physical Pin 11 (BCM GPIO 17)
#define LED_PIN 0

int main(int argc, char** argv){
    // Get args.
    char* port = NULL; 
    char* logPath = NULL;

    bool havePort = false;
    bool haveLog = false;
    int opt;
    // Server uses -p for port and -s for log file according to instructions
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

	// Hardware Setup
    if(wiringPiSetup() == -1){
        printf("Hardware err: Failed to initialize WiringPi.\n");
        return 1;
    }
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); // Ensure the LED starts in the OFF state
    
    // Pass NULL so it binds to INADDR_ANY (0.0.0.0) to accept external connections
    int sock = SetupServerSocket(NULL, port);
    
    // Allocate a single static buffer for all sends/receives
    void* buffer = malloc(PACKET_SIZE);
    
    while(1){
        uint32_t selfIsn = (uint32_t)rand();
        uint32_t curSeq;
        uint32_t theirSeq;
        uint16_t blinks = 0;
        uint16_t duration = 0;
            
        // Reset timeout to 0 (block indefinitely) waiting for a new client
        struct timeval timeOpt = {0,0};
        if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
            perror("setsockopt err");
            return errno;
        }
        
        // Put addr info on the stack to prevent memory leaks in the while(1) loop
        struct sockaddr_storage theirAddr;
        socklen_t theirSize = sizeof(theirAddr);
        
        // STEP 1: Handshake (Wait for SYN)
        printf("\nWaiting for a new client connection...\n");
        int numbytes = GetBuffer(&theirAddr, &theirSize, buffer, sock);
        if(CheckRecv(numbytes, HEADER_SIZE)) continue;

        Packet synPacket = PacketDeserialize(buffer, numbytes);
        if(synPacket.flags != FLAG_SYN){
            printf("GetBuffer err: SYN flag incorrect. Got %u\n", synPacket.flags);
            continue;
        }

        theirSeq = synPacket.seq;
        LogPacket(logPath, 1, synPacket);
        printf("Received SYN.\nSending SYN+ACK...\n");

        Packet synackPacket = MakePacket(selfIsn, theirSeq + 1, NULL, 0, FLAG_ACK | FLAG_SYN);
        PacketSerialize(buffer, synackPacket);

        bool transFail = true;
        int tries = -1;
        
        // Handshake: Send SYN+ACK, wait for ACK
        while(transFail){
            transFail = false;
            tries++;
            if(tries >= MAX_RETRIES){
                printf("Attempted %i times. Client dropped.\n", tries);
                break;
            }
            
            numbytes = SendBuffer((struct sockaddr*)&theirAddr, buffer, sock, HEADER_SIZE);
            if(CheckSend(numbytes, HEADER_SIZE)){ transFail = true; continue; }
            LogPacket(logPath, 0, synackPacket);

            // Set timeout for receiving the ACK
            timeOpt = (struct timeval){TIMEOUT_SEC, TIMEOUT_SEC};
            if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt)) == -1){
                perror("setsockopt err");
                free(buffer);
                return errno;
            }

            numbytes = GetBuffer(&theirAddr, &theirSize, buffer, sock);
            if(numbytes == -1) { transFail = true; continue; } // Timeout occurred
            if(CheckRecv(numbytes, HEADER_SIZE)) { transFail = true; continue; }
            
            Packet ackPacket = PacketDeserialize(buffer, numbytes);
            if(ackPacket.flags != FLAG_ACK){
                printf("Getbuffer err: Expected ACK, got flags %u\n", ackPacket.flags);
                transFail = true;
                if(ackPacket.payload) free(ackPacket.payload);
                continue;
            }
            LogPacket(logPath, 1, ackPacket);
            theirSeq = ackPacket.seq; // Update their sequence number
            if(ackPacket.payload) free(ackPacket.payload);
        }
        
        if(tries >= MAX_RETRIES) continue; // Go back to waiting for a new client
        
        curSeq = selfIsn + 1;
        printf("Handshake complete.\n");

        // STEP 2: Get blink params.
        numbytes = GetBuffer(&theirAddr, &theirSize, buffer, sock);
        if(CheckRecv(numbytes, HEADER_SIZE + BLINK_SIZE)) continue;

        Packet blinkPacket = PacketDeserialize(buffer, numbytes);
        LogPacket(logPath, 1, blinkPacket);
        
        // Update their sequence number strictly by payload size
        theirSeq += blinkPacket.length;

        UnpackBlink(blinkPacket.payload, &duration, &blinks);
        printf("Blink params set: %u blinks for %ums.\n", blinks, duration);

        // STEP 3: ACK blink params.
        Packet blinkAckPacket = MakePacket(curSeq, theirSeq, blinkPacket.payload, BLINK_SIZE, FLAG_ACK);
        PacketSerialize(buffer, blinkAckPacket);
        
        numbytes = SendBuffer((struct sockaddr*)&theirAddr, buffer, sock, HEADER_SIZE + BLINK_SIZE);
        if(CheckSend(numbytes, HEADER_SIZE + BLINK_SIZE)) { free(blinkPacket.payload); continue; }
        LogPacket(logPath, 0, blinkAckPacket);
        
        curSeq += blinkAckPacket.length;
        free(blinkPacket.payload);

        // STEP 4: Wait for motion.
        printf("Waiting for motion packet...\n");
        
        // Remove timeout while waiting for motion (client could take minutes)
        timeOpt = (struct timeval){0,0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt));

        numbytes = GetBuffer(&theirAddr, &theirSize, buffer, sock);
        if(CheckRecv(numbytes, HEADER_SIZE + MOTION_MSG_LEN)) continue;

        Packet motionPacket = PacketDeserialize(buffer, numbytes);
        LogPacket(logPath, 1, motionPacket);
        
        theirSeq += motionPacket.length;

        // Ensure it's null-terminated safely
        char motionRecv[MOTION_MSG_LEN + 1];
        memset(motionRecv, 0, sizeof(motionRecv));
        memcpy(motionRecv, (char*)motionPacket.payload, MOTION_MSG_LEN);
        
        if(strcmp(motionRecv, MOTION_MSG) != 0){
            printf("Payload err: Expected \"%s\", got \"%s\".\n", MOTION_MSG, motionRecv);
            free(motionPacket.payload);
            continue;
        }
        
        free(motionPacket.payload);
        printf("Motion detected message verified.\n");

        /*
            Blink mf.
        */
        printf("Driving LED: %u blinks at %ums duration.\n", blinks, duration);
        
        for(int i = 0; i < blinks; i++){
            digitalWrite(LED_PIN, HIGH);
            delay(duration);
            digitalWrite(LED_PIN, LOW);
            delay(duration);
        }

        // STEP 5: Send ACK for Motion
        printf("Sending ACK for motion.\n");
        Packet ackPacket = MakePacket(curSeq, theirSeq, NULL, 0, FLAG_ACK);
        PacketSerialize(buffer, ackPacket);
        
        numbytes = SendBuffer((struct sockaddr*)&theirAddr, buffer, sock, HEADER_SIZE);
        if(CheckSend(numbytes, HEADER_SIZE)) continue;
        LogPacket(logPath, 0, ackPacket);

        // STEP 6: Get FIN.
        // Set timeout back to a reasonable amount just in case client drops
        timeOpt = (struct timeval){5, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeOpt, sizeof(timeOpt));
        
        numbytes = GetBuffer(&theirAddr, &theirSize, buffer, sock);
        if(CheckRecv(numbytes, HEADER_SIZE)) continue;

        Packet finPacket = PacketDeserialize(buffer, numbytes);
        LogPacket(logPath, 1, finPacket);
        if(finPacket.flags != FLAG_FIN){
            printf("Incorrect FIN. Got %u\n", finPacket.flags);
            if(finPacket.payload) free(finPacket.payload);
            continue;
        }
        if(finPacket.payload) free(finPacket.payload);

        // STEP 7: Send FIN+ACK.
        Packet finackPacket = MakePacket(curSeq, finPacket.seq + 1, NULL, 0, FLAG_FIN | FLAG_ACK);
        PacketSerialize(buffer, finackPacket);
        numbytes = SendBuffer((struct sockaddr*)&theirAddr, buffer, sock, HEADER_SIZE);
        if(CheckSend(numbytes, HEADER_SIZE)) continue;

        LogFinish(logPath, &theirAddr);
        printf("Connection closed cleanly. Ready for next client.\n");
    }

    free(buffer);
    printf("Exiting...\n");
    return 0;
}