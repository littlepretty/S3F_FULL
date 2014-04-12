SRC   = simctrl.cc ssimctrl.cc dsimctrl.cc vtime_syscall.cc lookahead.cc asyncmsg.cc
HDR  = $(SRC:.cc=.h)
OBJ  = $(SRC:.cc=.o)
OPENVZEMU_INCLUDES=../openvzemu_include
CC	= g++
DEBUG	= -g
CFLAGS	= -Wall -c $(DEBUG) -I../ -I$(OPENVZEMU_INCLUDES) -DOPENVZ_EMULATION

sc.a	: $(OBJ)
	rm -f $@
	ar cq sc.a $(OBJ)

%.o : %.cc $(HDR)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o *.a

s3fnet: sc.a
	cd ../s3fnet/; make; cd ../simctrl

