#!/bin/bash

make
log=$(date +"%Y-%m-%d_%H-%M-%S")
ipstr=$(<addr)
if [[ $1 == "-v" ]]; then
    valgrind --leak-check=summary \
        --track-origins=yes \
        ./client -s ${ipstr} -p 8008 -l "logs/client_${log}.log" -f "res/artofrally_1.jpg"
else
    ./client -s ${ipstr} -p 8008 -l "logs/client_${log}.log" -f "res/artofrally_1.jpg"
fi
errno $?