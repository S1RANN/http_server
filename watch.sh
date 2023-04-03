#!/bin/bash

while true
do
	echo "number of opened files: $(ls /proc/$1/fd/ | wc -l)"
	sleep 0.05
done
