#!/bin/bash

compiler="./clox-debug"
dir="./test"
for file in `find ${dir} -name '*.lox'`
do
    echo $file
	$compiler $file
done	
echo "Test Done"