#!/bin/sh

~/sh/vsyscall.sh 0
brctl addbr vzbr1
brctl addbr vzbr2
brctl addbr vzbr3

#host1
vzctl start 211
vzctl exec 211 ifconfig venet0 down
vzctl exec 211 ifconfig lo 0
vzctl exec 211 ifconfig eth0 0
ifconfig veth211.0 0
echo 1 > /proc/sys/net/ipv4/conf/veth211.0/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth211.0/proxy_arp
vzctl exec 211 ifconfig eth0 10.10.0.18 netmask 255.255.255.0 broadcast 10.10.0.255 
#vzctl exec 211 ifconfig eth0 promisc
ifconfig veth211.0 10.10.0.18 netmask 255.255.255.0 broadcast 10.10.0.255
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
vzctl exec 212 ifconfig eth0 10.10.0.22 netmask 255.255.255.0 broadcast 10.10.0.255
#vzctl exec 212 ifconfig eth0 promisc
ifconfig veth212.0 10.10.0.22 netmask 255.255.255.0 broadcast 10.10.0.255
#ifconfig veth212.0 promisc
brctl addif vzbr2 veth212.0
echo "VE212 done"
sleep 2

#switch
vzctl start 209
vzctl exec 209 ifconfig venet0 down
vzctl exec 209 ifconfig lo 0
vzctl exec 209 ifconfig eth0 0
vzctl exec 209 ifconfig eth1 0
vzctl exec 209 ifconfig eth2 0
ifconfig veth209.0 0
ifconfig veth209.1 0
ifconfig veth209.2 0
echo 1 > /proc/sys/net/ipv4/conf/veth209.0/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth209.0/proxy_arp
echo 1 > /proc/sys/net/ipv4/conf/veth209.1/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth209.1/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth209.2/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth209.2/proxy_arp
vzctl exec 209 ifconfig eth0 10.10.0.17 netmask 255.255.255.0 broadcast 10.10.0.255
vzctl exec 209 ifconfig eth0 promisc
vzctl exec 209 ifconfig eth1 10.10.0.21 netmask 255.255.255.0 broadcast 10.10.0.255
vzctl exec 209 ifconfig eth1 promisc
vzctl exec 209 ifconfig eth2 10.10.0.25 netmask 255.255.255.0 broadcast 10.10.0.255
vzctl exec 209 ifconfig eth2 promisc
vzctl exec 209 ip route add 10.10.0.18 dev eth0
vzctl exec 209 ip route add 10.10.0.22 dev eth1
vzctl exec 209 ip route add 10.10.0.26 dev eth2
ifconfig veth209.0 10.10.0.17 netmask 255.255.255.0 broadcast 10.10.0.255
ifconfig veth209.0 promisc
ifconfig veth209.1 10.10.0.21 netmask 255.255.255.0 broadcast 10.10.0.255
ifconfig veth209.1 promisc
ifconfig veth209.2 10.10.0.25 netmask 255.255.255.0 broadcast 10.10.0.255
ifconfig veth209.2 promisc
brctl addif vzbr1 veth209.0
brctl addif vzbr2 veth209.1
brctl addif vzbr3 veth209.2
vzctl set 209 --devnodes net/tun:rw --save
vzctl set 209 --devices c:10:200:rw --save
vzctl set 209 --capability net_admin:on --save
vzctl exec 209 mkdir -p /dev/net
vzctl exec 209 chmod 660 /dev/net/tun
echo "VE209 done"
sleep 2

#controller
vzctl start 210
vzctl exec 210 ifconfig venet0 down
vzctl exec 210 ifconfig lo 0
vzctl exec 210 ifconfig eth0 0
ifconfig veth210.0 0
echo 1 > /proc/sys/net/ipv4/conf/veth210.0/forwarding
echo 1 > /proc/sys/net/ipv4/conf/veth210.0/proxy_arp
vzctl exec 210 ifconfig eth0 10.10.0.26 netmask 255.255.255.0 broadcast 10.10.0.255
#vzctl exec 210 ifconfig eth0 promisc
ifconfig veth210.0 10.10.0.26 netmask 255.255.255.0 broadcast 10.10.0.255
#ifconfig veth210.0 promisc
brctl addif vzbr3 veth210.0
echo "VE210 done"
sleep 2

ifconfig vzbr1 0
ifconfig vzbr2 0
ifconfig vzbr3 0
ifconfig vzbr1 down
ifconfig vzbr2 down
ifconfig vzbr3 down
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
