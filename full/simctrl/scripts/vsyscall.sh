if [ $# -lt 1 ]; then
	echo "set vsyscall flag"
	echo "usage: $0 <0|1>"
	exit 1
fi

echo $1 > /proc/sys/kernel/vsyscall
echo $1 > /proc/sys/kernel/vsyscall64

