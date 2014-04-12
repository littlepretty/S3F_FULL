vzctl stop 211
echo "VE211 done"
vzctl stop 212
echo "VE212 done"
vzctl stop 213
echo "VE213 done"
ifconfig vzbr1 down
ifconfig vzbr2 down
sleep 2
brctl delbr vzbr1
brctl delbr vzbr2
echo "done"
