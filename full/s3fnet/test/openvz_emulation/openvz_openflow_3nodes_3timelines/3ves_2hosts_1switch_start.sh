#!/bin/sh

~/sh/vsyscall.sh 0
brctl addbr vzbr1
brctl addbr vzbr2

#host1
vzctl start 211
vzctl exec 211 ifconfig venet0 down
vzctl exec 211 ifconfig lo 0
vzctl exec 211 ifconfig eth0 0
ifconfig veth211.0 0
echo 1 > /proc/sys/net/ipv4/conf/veth211.0/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth211.0/proxy_arp
vzctl exec 211 ifconfig eth0 10.10.0.13 netmask 255.255.255.0 broadcast 10.10.0.255 
#vzctl exec 211 ifconfig eth0 promisc
ifconfig veth211.0 10.10.0.13 netmask 255.255.255.0 broadcast 10.10.0.255
#ifconfig veth211.0 promisc
brctl addif vzbr1 veth211.0
echo "VE211 done"
sleep 2

#host2
vzctl start 212
vzctl exec 212 ifconfig venet0 down
vzctl exec 212 ifconfig lo 0
vzctl exec 212 ifconfig eth0 0
ifconfig veth212.0 0
echo 1 > /proc/sys/net/ipv4/conf/veth212.0/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth212.0/proxy_arp
vzctl exec 212 ifconfig eth0 10.10.0.17 netmask 255.255.255.0 broadcast 10.10.0.255
#vzctl exec 212 ifconfig eth0 promisc
ifconfig veth212.0 10.10.0.17 netmask 255.255.255.0 broadcast 10.10.0.255
#ifconfig veth212.0 promisc
brctl addif vzbr2 veth212.0
echo "VE212 done"
sleep 2

#switch
vzctl start 213
vzctl exec 213 ifconfig venet0 down
vzctl exec 213 ifconfig lo 0
vzctl exec 213 ifconfig eth0 0
vzctl exec 213 ifconfig eth1 0
ifconfig veth213.0 0
ifconfig veth213.1 0
echo 1 > /proc/sys/net/ipv4/conf/veth213.0/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth213.0/proxy_arp
echo 1 > /proc/sys/net/ipv4/conf/veth213.1/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth213.1/forwarding
vzctl exec 213 ifconfig eth0 10.10.0.14 netmask 255.255.255.0 broadcast 10.10.0.255
vzctl exec 213 ifconfig eth0 promisc
vzctl exec 213 ifconfig eth1 10.10.0.18 netmask 255.255.255.0 broadcast 10.10.0.255
vzctl exec 213 ifconfig eth1 promisc
vzctl exec 213 ip route add 10.10.0.13 dev eth0
vzctl exec 213 ip route add 10.10.0.17 dev eth1
ifconfig veth213.0 10.10.0.14 netmask 255.255.255.0 broadcast 10.10.0.255
ifconfig veth213.0 promisc
ifconfig veth213.1 10.10.0.18 netmask 255.255.255.0 broadcast 10.10.0.255
ifconfig veth213.1 promisc
brctl addif vzbr1 veth213.0
brctl addif vzbr2 veth213.1
vzctl set 213 --devnodes net/tun:rw --save
vzctl set 213 --devices c:10:200:rw --save
vzctl set 213 --capability net_admin:on --save
vzctl exec 213 mkdir -p /dev/net
vzctl exec 213 chmod 660 /dev/net/tun
echo "VE213 done"
sleep 2

ifconfig vzbr1 0
ifconfig vzbr2 0
ifconfig vzbr1 down
ifconfig vzbr2 down
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
