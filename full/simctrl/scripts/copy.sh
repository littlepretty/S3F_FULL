#!/bin/sh

if [ $# -lt 4 ]; then
    echo "copy a file to all VEs"
    echo "usage: $0 <from VE ID> <to VE ID> <target> <destination>"
    exit 1
fi

i=$1
while [ $i -le $2 ]
do
    cp -P -R $3 /vz/private/$i/$4

    echo "VE$i done"
    i=$[i+1]
done

