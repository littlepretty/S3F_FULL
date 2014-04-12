#!/bin/sh

if [ $# -lt 3 ]; then
    echo "create VEs"
    echo "usage: $0 <from VE ID> <to VE ID> <VE template>"
    exit 1
fi

i=$1
while [ $i -le $2 ]
do
    vzctl create $i --ostemplate $3
    vzctl set $i --onboot no --netif_add eth0 --cpus 1 --save
    touch /vz/private/$i/root/this_is_ve$i

    mkdir /vz/private/$i/etc/init.d.disable
    mv /vz/private/$i/etc/init.d/crond /vz/private/$i/etc/init.d.disable
    mv /vz/private/$i/etc/init.d/sendmail /vz/private/$i/etc/init.d.disable

    echo "VE$i done"
    i=$[i+1]
done

