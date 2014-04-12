#!/bin/sh

if [ $# -lt 3 ]; then
    echo "remove a file in all VEs"
    echo "usage: $0 <from VE ID> <to VE ID> <target>"
    exit 1
fi

i=$1
while [ $i -le $2 ]
do
    rm -rf /vz/private/$i/$3

    echo "VE$i done"
    i=$[i+1]
done

