#include "protocol.h"

/* SetupServerSocket(addr, port)
    Setups a server socket.
    Returns socket.
*/
int SetupServerSocket(char* addr, char* port){
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

    if(strcmp(addr, "localhost") == 0 || strcmp(addr, "127.0.0.1") == 0){
        FILE* file = fopen("addr", "w");
        if(file) {
            fprintf(file, "%s", foundAddr);
            fclose(file);
        }
    }
    
    freeaddrinfo(info); // Clean up memory!
    return sock;
}

/* MakePacket(seq, ack, payload, length, flags)
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

/* PacketSerialize(packet);
    Morphs packet into byte array using strict 12-byte header.
*/
void PacketSerialize(void* buffer, Packet packet){
    uint32_t* header = (uint32_t*)buffer;
    header[0] = htonl(packet.seq);
    header[1] = htonl(packet.ack);
    header[2] = htonl(packet.flags);
    
    // Copy payload right after the 12th byte
    if(packet.length > 0 && packet.payload != NULL) {
        memcpy((char*)buffer + HEADER_SIZE, packet.payload, packet.length);
    }
}

/* PacketDeserialize(buffer, recv_len)
    Morphs buffer into packet struct, calculates payload length from total received.
*/
Packet PacketDeserialize(void* buffer, int recv_len){
    uint32_t* header = (uint32_t*)buffer;
    uint32_t seq = ntohl(header[0]);
    uint32_t ack = ntohl(header[1]);
    uint32_t flags = ntohl(header[2]);
    
    // Payload length is total bytes received minus the 12-byte header
    int payload_length = recv_len - HEADER_SIZE;
    
    if(payload_length <= 0) return MakePacket(seq, ack, NULL, 0, flags);
    
    void* ptr = malloc(payload_length);
    memcpy(ptr, (char*)buffer + HEADER_SIZE, payload_length);
    
    return MakePacket(seq, ack, ptr, payload_length, flags);
}

/* PackBlink(payload, duration, times) */
void PackBlink(void* payload, uint16_t duration, uint16_t times){
    uint16_t durPack = htons(duration);
    uint16_t timesPack = htons(times);
    memcpy(payload, &durPack, sizeof(uint16_t));
    memcpy((char*)payload + sizeof(uint16_t), &timesPack, sizeof(uint16_t));
}

/* UnpackBlink(payload, durPtr, timesPtr) */
void UnpackBlink(void* payload, uint16_t* durPtr, uint16_t* timesPtr){
    *durPtr = ntohs(*((uint16_t*)payload));
    *timesPtr = ntohs(*((uint16_t*)((char*)payload + sizeof(uint16_t))));
}

/* PackMotion(payload) */
void PackMotion(void* payload){
    strcpy((char*)payload, MOTION_MSG);
}

/* LogPacket(log, recv, packet)
    Strictly follows rubric format.
*/
int LogPacket(char* log, int recv, Packet packet){
    FILE* file = fopen(log, "a");
    if(!file) return 0;

    char flags_str[32] = "";
    if(packet.flags & FLAG_ACK) strcat(flags_str, " \"ACK\"");
    if(packet.flags & FLAG_SYN) strcat(flags_str, " \"SYN\"");
    if(packet.flags & FLAG_FIN) strcat(flags_str, " \"FIN\"");

    char* ts = Timestamp();
    
    // Format: [TIMESTAMP] "RECV" <Seq> <Ack> ["ACK"] ["SYN"] ["FIN"]
    fprintf(file, "[%s] \"%s\" %u %u%s\n",
        ts,
        (recv) ? "RECV" : "SEND",
        packet.seq,
        packet.ack,
        flags_str);
        
    free(ts); // Fixed memory leak
    fflush(file);
    fclose(file);

    return 1;
}

int LogFinish(char* log, struct sockaddr_storage* info){
    FILE* file = fopen(log, "a");
    if(!file) return 0;
    
    char addr[INET_ADDRSTRLEN];
    inet_ntop(info->ss_family, &(((struct sockaddr_in*)info)->sin_addr), addr, INET_ADDRSTRLEN);
    
    fprintf(file, ":Interaction with %s completed.\n", addr);
    fflush(file);
    fclose(file);

    return 1;
}

/* Timestamp() */
char* Timestamp(){
    char* buffer = malloc(50);
    time_t now = time(NULL);
    strftime(buffer, 50, "%Y-%m-%d-%H-%M-%S", localtime(&now));
    return buffer;
}

/* GetBuffer(info, infolen, buffer, sock)
    Receives ONE UDP datagram. No while-loop needed for UDP.
*/
int GetBuffer(struct sockaddr_storage* info, socklen_t* infolen, void* buffer, int sock){
    int got = recvfrom(sock, buffer, PACKET_SIZE, 0, (struct sockaddr*)info, infolen);
    if(got == -1){
        perror("recvfrom err");
        if(errno == EFAULT) exit(errno);
        return -1;
    }

    char foundAddr[INET_ADDRSTRLEN];
    printf("recvfrom %s.\n", inet_ntop(info->ss_family, &(((struct sockaddr_in*)info)->sin_addr), foundAddr, INET_ADDRSTRLEN));
    printf("\tReceived %i bytes.\n", got);

    return got;
}

/* SendBuffer(info, buffer, sock, size)
    Sends ONE UDP datagram. No while-loop needed for UDP.
*/
int SendBuffer(struct sockaddr* info, void* buffer, int sock, int size){
    int sent = sendto(sock, buffer, size, 0, info, sizeof(*info));
    if(sent == -1){
        perror("sendto err");
        return -1;
    }

    printf("\tSent %i bytes.\n", sent);
    return sent;
}

/* CheckRecv(numbytes, size) */
bool CheckRecv(int numbytes, int size){
    if(numbytes < size){
        printf("\tWarning: Expected at least %u bytes, got %u.\n", size, numbytes);
        return true; // Technically UDP might just be a short packet, but good for header checks
    }
    return false;
}

/* CheckSend(numbytes, size) */
bool CheckSend(int numbytes, int size){
    if(numbytes < size){
        printf("\tError: Only sent %u out of %u bytes.\n", numbytes, size);
        return true;
    }
    return false;
}