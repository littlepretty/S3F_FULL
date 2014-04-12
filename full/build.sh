#!/bin/sh

USAGE="Usage: `basename $0` [-h] [-n arg] [-f] [-i] [-s] [-l] [-d]" 
PARAMETER="-n\t number of cores used to run make\n-f\t skip building those libraries that only need to be built once (DML, metis, openflowlib)\n-i\t incremental build (update) instead of clean build\n-s\t simulation only, not building emulation\n-l\t enable emulation lookahead\n-d\t display S3FNet debug messages\n-h\t display the usage"
OVERVIEW="Script for building the S3F project"
EXAMPLE="e.g., `basename $0` -n 4 -f \n Use 4 cores to clean build the S3F project excluding those libraries that only need to be built once"

#default values
nc=1  # number core used to build the project = 1
dml=1 # to build dml  
inc=0 # no incremental build, i.e., clean build
sim=1 # to build emulation (default we should set sim = 0 to build emulation; however, if to build phold_node, sim must be 1)
debug=0 # not show s3fnet debug messages
lookahead=0 # not use emulation lookahead

# Parse command line options
while getopts hn:fisdlo OPT; do
    case "$OPT" in 
        h)
            echo $OVERVIEW
            echo $USAGE
            echo $PARAMETER
            echo ""
            echo $EXAMPLE
            echo ""
            exit 0
            ;;
        n)
            nc=$OPTARG
            if [ $nc -le 0 ]; then
                nc=1
            fi
            ;;
        f)
            dml=0
            ;;
        i)
            inc=1
            ;;
        s)
            sim=1
            ;;
        d)
            debug=1
            ;;
        l)
            lookahead=1
            ;;
        \?)
            # getopts issues an error message
            echo $OVERVIEW
            echo $USAGE >&2
            echo $PARAMETER
            echo ""
            echo $EXAMPLE
            echo ""
            exit 1
            ;;
        esac
done

# Remove the switches we parsed above
shift `expr $OPTIND - 1`

if [ $dml -eq 1 ] || [ ! -d "openvzemu_include" ]; then
rm -rf openvzemu_include
echo "Creating openvemu_include folder ... "
tar xvfz openvzemu_include.tar.gz 
else
echo "openvzemu_include folder exists"
echo ""
fi
	
echo "--------------------------------"
echo "Buidling s3f/api ... "
echo "--------------------------------"
cd api
if [ $inc -eq 0 ]; then
rm *.o *.a
fi
if [ $sim -eq 0 ]; then
arg="ENABLE_OPENVZ_EMULATION=yes"
else
arg="ENABLE_OPENVZ_EMULATION=no"
fi
if [ $lookahead -eq 0 ]; then
arg="$arg ENABLE_OPENVZ_EMULATION_LOOKAHEAD=no"
else
arg="$arg ENABLE_OPENVZ_EMULATION_LOOKAHEAD=yes"
fi
make -j$nc $arg
cd ..

echo "--------------------------------"
echo "Buidling s3f/rng ... "
echo "--------------------------------"
cd rng
if [ $inc -eq 0 ]; then
rm *.o *.a
fi
make -j$nc
cd ..

echo "--------------------------------"
echo "Buidling s3f/aux ... "
echo "--------------------------------"
cd aux
if [ $inc -eq 0 ]; then
rm *.o *.a
fi
make -j$nc
cd ..

if [ $sim -eq 1 ]; then
echo "--------------------------------"
echo "Buidling s3f/app ... "
echo "--------------------------------"
cd app
if [ $inc -eq 0 ]; then
rm *.o
fi
make -j$nc
cd ..
fi

if [ $sim -eq 0 ]; then
echo "--------------------------------"
echo "Buidling s3f/simctrl ... "
echo "--------------------------------"
cd simctrl
if [ $inc -eq 0 ]; then
rm *.o *.a
fi
make -j$nc
cd ..
fi

if [ $dml -eq 1 ] && [ $inc -eq 0 ]; then
echo "--------------------------------"
echo "Buidling s3f/dml ... "
echo "--------------------------------"
cd dml
aclocal
autoconf
automake
./configure
make clean
make
cd ..

echo "--------------------------------"
echo "Buidling s3f/metis ... "
echo "--------------------------------"
cd metis
rm *.o *.a 
sh metis.sh
cd ..
fi

if [ $dml -eq 1 ] || [ ! -d "openflowlib/build" ]; then
echo "--------------------------------"
echo "Buidling s3f/openflowlib ... "
echo "--------------------------------"
cd openflowlib
./waf configure
./waf clean
./waf build
cd ..
fi


echo "--------------------------------"
echo "Buidling s3f/s3fnet ... "
echo "--------------------------------"
cd s3fnet
if [ $inc -eq 0 ]; then
make clean
fi
if [ $sim -eq 0 ]; then
arg="ENABLE_OPENVZ_EMULATION=yes"
else
arg="ENABLE_OPENVZ_EMULATION=no"
fi
if [ $lookahead -eq 0 ]; then
arg="$arg ENABLE_OPENVZ_EMULATION_LOOKAHEAD=no"
else
arg="$arg ENABLE_OPENVZ_EMULATION_LOOKAHEAD=yes"
fi
if [ $debug -eq 0 ]; then
arg="$arg ENABLE_S3FNET_DEBUG=no"
else
arg="$arg ENABLE_S3FNET_DEBUG=yes"
fi
make -j$nc $arg
cd ..
