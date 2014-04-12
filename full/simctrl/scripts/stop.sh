#!/bin/sh

if [ $# -lt 2 ]; then
    echo "stop VEs"
    echo "usage: $0 <from VE ID> <to VE ID>"
    exit 1
fi

i=$1
while [ $i -le $2 ]
do
    vzctl stop $i &

    echo "VE$i done"
    sleep 1
    i=$[i+1]
done

if [ $(vzlist | wc -l) -le 1 ]; then
    ifconfig vzbr down
    brctl delbr vzbr
    echo "vzbr deleted"
fi
