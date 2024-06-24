#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NOCOLOR='\033[0m'

compiler="./bin/clox-debug"
dir="./test"
echo > testInformation
for file in `find ${dir} -name '*.lox'`
do
    echo ${YELLOW}$file${NOCOLOR}

    start_time=$(date +%s.%N)
    echo $file >> testInformation
	$compiler $file >> testInformation
    echo >> testInformation
    end_time=$(date +%s.%N)
    runtime=$(echo "sacle=3; ($end_time - $start_time) * 1000" | bc)

    if [ $? -eq 0 ]; then
        echo "${GREEN}Run Success${NOCOLOR} Executed in $runtime ms"
    else
        echo "${RED}Run Error${NOCOLOR}"
    fi
done
echo "=====Test Done====="