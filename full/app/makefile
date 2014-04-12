# To build phold_node, ENABLE_OPENVZ_EMULATION=no must be set when building s3f api
SRC   = phold_node.cc 
HDR  = $(SRC:.cc=.h)
OBJ  = $(SRC:.cc=.o)
#DEFINE = -DSAFE_EVT_PTR -DSAFE_MSG_PTR
CC	= g++
DEBUG	= -g
CFLAGS	= -Wall -c $(DEBUG) $(DEFINE) -I../ -I../openvzemu_include -O3
LFLAGS	= -Wall $(DEBUG) -L../api -L../aux -L../rng -L../openvzemu_include # -L../simctrl -L/usr/local/include
LFLAGS2 = -lpthread
S3FLIB	= ../api/s3f.a
RNDLIB  = ../rng/rng.a
AUXLIB  = ../aux/aux.a
#SIMCTRLLIB = ../simctrl/sc.a
#FANNLIB = /usr/local/lib/libfann.a
#PCAPLIB = /usr/lib/x86_64-linux-gnu/libpcap.a
#S3FNETLIB = ../s3fnet/src/net/*.o

phold_node	: phold_node.o $(S3FLIB) $(RNDLIB) $(AUXLIB) #$(SIMCTRLLIB) $(FANNLIB) $(PCAPLIB) $(S3FNETLIB)
	$(CC) -o phold_node phold_node.o $(LFLAGS) $(S3FLIB) $(RNDLIB) $(AUXLIB) $(LFLAGS2)

phold_node.o	: $(SRC) $(HDR) 
	$(CC) $(CFLAGS) -c $<
clean		:
	rm *.o phold_node
