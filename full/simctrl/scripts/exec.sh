#!/bin/sh

if [ $# -lt 3 ]; then
    echo "execuate a command in all VEs (requires running VEs)"
    echo "usage: $0 <from VE ID> <to VE ID> <command> <parameters...>"
    exit 1
fi

i=$1
while [ $i -le $2 ]
do
    vzctl exec $i $3 $4 $5 $6 $7 $8 $9

    echo "VE$i done"
    i=$[i+1]
done

