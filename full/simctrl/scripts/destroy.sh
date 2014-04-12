#!/bin/sh

if [ $# -lt 2 ]; then
    echo "destroy VEs"
    echo "usage: $0 <from VE ID> <to VE ID>"
    exit 1
fi

i=$1
while [ $i -le $2 ]
do
    vzctl destroy $i
    echo "VE$i done"
    i=$[i+1]
done

