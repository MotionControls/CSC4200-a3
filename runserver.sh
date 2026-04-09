#!/bin/bash

make
log=$(date +"%Y-%m-%d_%H-%M-%S")
if [[ $1 == "-v" ]]; then
    valgrind --leak-check=summary \
        --track-origins=yes \
        ./server -p 8008 -s "logs/server_${log}.log"
else
    ./server -p 8008 -s "logs/server_${log}.log"
fi
errno $?