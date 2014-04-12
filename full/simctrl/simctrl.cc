/** \file simctrl.cc
 * \brief source file for the abstract VE controller
 *
 * authors : Yuhao Zheng
 *           Dong (Kevin) Jin
 */

#include <simctrl/vtime_syscall.h>
#include <simctrl/lookahead.h>
#include <simctrl/asyncmsg.h>
#include <s3f.h>
#include <s3fnet/src/net/net.h>
#include <pcap.h>
#include <errno.h>


/*****************************/
/* EmuPacket class functions */
/*****************************/

/* constructor */
EmuPacket::EmuPacket(int len)
{
	this->dna = DNA;
	this->len = len;
	data = new unsigned char[len];
	ifid = 0;
}


/* destructor */
EmuPacket::~EmuPacket()
{
	if (data) delete data;
}


/* type checker */
bool EmuPacket::check(void *ptr)
{
	return ptr && ((EmuPacket*)ptr)->dna == DNA;
}


/* duplicate: return a duplicate copy */
EmuPacket* EmuPacket::duplicate()
{
	EmuPacket* ppkt = new EmuPacket(len);
	memcpy(ppkt->data, data, len);
	ppkt->timestamp = timestamp;
	ppkt->ifid = ifid;
	return ppkt;
}


/****************************/
/* VEHandle class functions */
/****************************/

/* constructor */
VEHandle::VEHandle(int veid, int la_type, int nif)
{
	id = veid;
	clock = 0;
	lastclock = 0;
	avgrun = VTIME_VTIMESLICE / 2;
	offset = 0;
	mindelay = 0;
	idle = false;
	idlecount = 0;
	tl = NULL;

	this->nif = nif;
	pcap.resize(nif);
	for (int i = 0; i < nif; i++) pcap[i] = NULL;

	switch (la_type) {
	case LOOKAHEAD_ANN:
		la = new LookaheadANN(this);
		break;
	default:
		la = NULL;
	}
}


/* destructor */
VEHandle::~VEHandle()
{
	/* delete send queue */
	while (!sendq.empty()) {
		EmuPacket *pkt = sendq.front();
		delete pkt;
		sendq.pop_front();
	}

	/* delete recv queue */
	while (!recvq.empty()) {
		EmuPacket *pkt = recvq.front();
		delete pkt;
		recvq.pop_front();
	}

	/* close pcap handles */
	for (int i = 0; i < nif; i++) if (pcap[i]) pcap_close((pcap_t*)pcap[i]);

	/* delete look handle */
	if (la) delete (Lookahead*)la;
}


/***************************/
/* SimCtrl class functions */
/***************************/

/* constructor */
SimCtrl::SimCtrl()
{
	siminf = NULL;
}


/* destructor */
SimCtrl::~SimCtrl()
{
}


/* obtain concrete SimCtrl handle */
SimCtrl* SimCtrl::get_simctrl(int type)
{
	switch (type)
	{
	case EMULATION_LOCAL:
		return new SSimCtrl;
	case EMULATION_DISTRIBUTED:
		return new DSimCtrl;
	default:
		return NULL;
	}
	return NULL;
}


/* human readable error code */
const char* SimCtrl::errcode2txt(int code)
{
	static char str[10];
	switch (-code)
	{
	case EPERM:     return "EPERM";
	case ENOENT:    return "ENOENT";
	case ESRCH:     return "ESRCH";
	case EINTR:     return "EINTR";
	case EIO:       return "EIO";
	case ENXIO:     return "ENXIO";
	case E2BIG:     return "E2BIG";
	case ENOEXEC:   return "ENOEXEC";
	case EBADF:     return "EBADF";
	case ECHILD:    return "ECHILD";
	case ENOMEM:    return "ENOMEM";
	case EACCES:    return "EACCES";
	case EFAULT:    return "EFAULT";
	case EBUSY:     return "EBUSY";
	case EEXIST:    return "EEXIST";
	case ENODEV:    return "ENODEV";
	case EINVAL:    return "EINVAL";
	case EPIPE:     return "EPIPE";
	case ERANGE:    return "ERANGE";
	}
	sprintf(str, "%d", code);
	return str;
}


/* inject emulation events to s3fnet, to be called by s3f/s3fnet */
void SimCtrl::inject_emu_event_to_s3fnet()
{
	if (siminf) ((s3f::s3fnet::SimInterface*)siminf)->topnet->injectEmuEvents();
}

