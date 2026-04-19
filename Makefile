CC      = gcc
#CFLAGS  = -Wall -Wextra -g -Iinclude
CFLAGS  = -Wall -Wextra -Wno-error=incompatible-pointer-types -g -Iinclude -ggdb3
#LDFLAGS =
LDFLAGS = -lm

OBJ_DIR = obj
SRC     = src

.PHONY: all clean

all: helper server client

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/protocol.o: $(SRC)/protocol.c include/protocol.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

helper:
	clear

server: $(SRC)/server.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

client: $(SRC)/client.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

play: $(SRC)/playground.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) server client received_* *.log
