#!/bin/sh

if [ $# -lt 2 ]; then
    echo "start VEs"
    echo "usage: $0 <from VE ID> <to VE ID>"
    exit 1
fi

./vsyscall.sh 0
if [ $(brctl show | grep vzbr | wc -l) -eq 0 ]; then
    brctl addbr vzbr
    echo "vzbr created"
fi

i=$1
while [ $i -le $2 ]
do
    vzctl start $i
    vzctl exec $i ifconfig venet0 down
    vzctl exec $i ifconfig lo 0
    vzctl exec $i ifconfig eth0 0
    ifconfig veth$i.0 0

    echo 0 > /proc/sys/net/ipv4/conf/veth$i.0/forwarding
    echo 0 > /proc/sys/net/ipv4/conf/veth$i.0/proxy_arp

    brctl addif vzbr veth$i.0

    echo "VE$i done"
    sleep 1
    i=$[i+1]
done

ifconfig vzbr 0
ifconfig vzbr down
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
