vzctl stop 211
echo "VE211 done"
vzctl stop 212
echo "VE212 done"
vzctl stop 209
echo "VE209 done"
vzctl stop 210
echo "VE210 done"
ifconfig vzbr1 down
ifconfig vzbr2 down
ifconfig vzbr3 down
sleep 2
brctl delbr vzbr1
brctl delbr vzbr2
brctl delbr vzbr3
echo "done"
