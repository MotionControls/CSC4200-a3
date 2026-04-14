#include "protocol.h"

int main(){
    uint16_t duration = 64;
    uint16_t times = 42;

    void* payload = malloc(sizeof(uint16_t)*2);
    PackBlink(payload, duration, times);
    Packet packet = MakePacket(0, 0, payload, sizeof(uint16_t)*2, 0);
    uint32_t* buffer = malloc(HEADER_SIZE + packet.length);
    PacketSerialize(buffer, packet);
    
    Packet newPacket = PacketDeserialize(buffer);
    UnpackBlink(payload, &duration, &times);

    printf("%us & %us\n", duration, times);

    payload = realloc(payload, MOTION_MSG_LEN);
    PackMotion(payload);
    packet = MakePacket(0, 0, payload, MOTION_MSG_LEN, 0);
    buffer = realloc(buffer, HEADER_SIZE + packet.length);
    PacketSerialize(buffer, packet);

    newPacket = PacketDeserialize(buffer);
    printf("%s\n", (char*)newPacket.payload);

    return 0;
}