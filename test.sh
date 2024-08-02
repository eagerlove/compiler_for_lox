#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NOCOLOR='\033[0m'

compiler="./bin/clox-debug"
dir="./test"
echo > testInformation
for file in $(find ${dir} -name '*.lox'); do
    start_time=$(date +%s.%N)
    len=${#file}
    len=$((len - 7))

    echo ${file##*/} >> testInformation
	$compiler $file >> testInformation
    exit_state=$?

    echo >> testInformation
    end_time=$(date +%s.%N)
    runtime=$(echo "scale=3; ($end_time - $start_time) * 1000" | bc)

    if [ $exit_state -eq 0 ]; then
        if [ $len -lt 8 ]; then
            echo "${YELLOW}${file##*/}${NOCOLOR}\t\t${GREEN}Run Success${NOCOLOR}\tExecuted in $runtime ms"
        else
            echo "${YELLOW}${file##*/}${NOCOLOR}\t${GREEN}Run Success${NOCOLOR}\tExecuted in $runtime ms"
        fi
    else
        echo "Run Error" >> testInformation
        echo "${YELLOW}${file##*/}${NOCOLOR} ${RED}Run Error${NOCOLOR}"
    fi
done

echo "=====Test Done====="