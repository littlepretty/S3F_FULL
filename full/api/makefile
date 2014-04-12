ENABLE_OPENVZ_EMULATION = yes
ENABLE_OPENVZ_EMULATION_LOOKAHEAD = no

SRC   = interface.cc entity.cc message.cc inchannel.cc interface.cc outchannel.cc process.cc timeline.cc event.cc
THDR = ../time/pq.h ../time/eventlist.h	../time/stl-eventlist.h

HDR  = $(SRC:.cc=.h) $(THDR) ../s3f.h
OPENVZEMU_INCLUDES=../openvzemu_include
OBJ  = $(SRC:.cc=.o) 
CC	= g++
#DEFINE  = -DSAFE_EVT_PTR -DSAFE_MSG_PTR

ifeq ($(ENABLE_OPENVZ_EMULATION), yes)
S3F_EMU_CFG = -DOPENVZ_EMULATION
else
S3F_EMU_CFG =
endif

ifeq ($(ENABLE_OPENVZ_EMULATION_LOOKAHEAD), yes)
S3FNET_EMU_LA_CFG = -DOPENVZ_EMULATION_LOOKAHEAD
else
S3FNET_EMU_LA_CFG =
endif

DEBUG	= -g
CFLAGS	= -Wall -c $(DEBUG) $(DEFINE) $(S3F_EMU_CFG) $(S3FNET_EMU_LA_CFG) -I../ -I$(OPENVZEMU_INCLUDES) -O3
LFLAGS	= -Wall $(DEBUG) 

all	: s3f.a

s3f.a	: $(OBJ)
	rm -f $@
	ar cq $@ $(OBJ)

%.o :	%.cc $(HDR)
	$(CC) $(CFLAGS) -c $<

clean:
	rm *.o *.a
	
