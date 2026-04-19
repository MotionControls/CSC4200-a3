CC = gcc
CFLAGS = -Wall -Wextra -g -I$(INC_DIR)
LDFLAGS = -lwiringPi

# Directories
SRC_DIR = src
INC_DIR = include

# Targets
all: lightserver lightclient

lightserver: server.o protocol.o
	$(CC) $(CFLAGS) -o lightserver server.o protocol.o $(LDFLAGS)

lightclient: client.o protocol.o
	$(CC) $(CFLAGS) -o lightclient client.o protocol.o $(LDFLAGS)

server.o: $(SRC_DIR)/server.c $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/server.c

client.o: $(SRC_DIR)/client.c $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/client.c

protocol.o: $(SRC_DIR)/protocol.c $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/protocol.c

clean:
	rm -f *.o lightserver lightclient *.log capture.pcap addr