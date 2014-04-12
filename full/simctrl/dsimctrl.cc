/** \file dsimctrl.cc
 * \brief source file for the distributed VE controller
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

#define EMU_MAX_VE 1000
#define MAX_IDLE_COUNT 1

#define TIME_STAT
#define SLAVE_LOG

#ifdef TIME_STAT
static double simtime = 1, emutime = 1, esw = 1, mswin = 1, avgla = 1, avgminla = 1;
static struct timeval tstv1, tstv2, lastprint;
static ltime_t lastvtime;
static bool firsttime = true;
static const int timestat_interval = 1;
static int timestat_counter = 0;
static double gtoddelay = -1;
static double syncoverhead = 0;

static inline void timestat_update(double &old, double add)
{
	old = old * 0.999 + (add < 0 ? 0 : add) * 0.001;
}
#endif

#ifdef SLAVE_LOG
static FILE *pfslave = NULL;
#endif

#define gotofail(code) { reason = code; goto FAIL; }

static VESlave *pslave = NULL;

static bool debug_nopause = true;
static const bool debug = false;


/***************************/
/* VESlave class functions */
/***************************/

/* constructor */
VESlave::VESlave()
{
	state = DISCONNECTED;
	selected = false;
	isinit = false;
	addr = NULL;
	amh = NULL;
	nthread = 1;
	nve = 0;
	laerr = 0;
	pslave = this;
}


/* destructor */
VESlave::~VESlave()
{
	if (addr) delete addr;
	if (amh) delete (AsyncMessageHandler*)amh;
}


/* master side connect slave */
int VESlave::master_connect()
{
	if (state != DISCONNECTED) return -EEXIST;
	if (!addr) return -EBADF;
	AsyncMessageHandler *amh = new AsyncMessageHandler;
	int ret = amh->amh_connect(addr);
	if (ret) { delete amh; return ret; }
	this->amh = amh;
	state = READY;
	isinit = false;
	return 0;
}


/* master side disconnect slave */
int VESlave::master_disconnect()
{
	if (state == DISCONNECTED) return 0;
	AsyncMessageHandler *amh = (AsyncMessageHandler*) this->amh;
	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::CLOSE;
	int ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;
	if (ret) return ret;
	ret = amh->amh_close();
	delete amh;
	this->amh = NULL;
	if (!ret) state = DISCONNECTED;
	return ret;
}


/* master side set VE IP address */
int VESlave::master_setip(int ctid, int ifid, const char* ip)
{
	int ret = 0;
	if (isinit) return -EPERM;
	if (state == DISCONNECTED) ret = master_connect();
	if (ret) return ret;
	if (state != READY) return -EBUSY;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;

	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::SETIP;
	m->int1 = ctid - idoffset;
	m->int2 = ifid;
	m->resize(strlen(ip) + 1);
	memcpy(m->payload, ip, m->payloadlen);
	ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;

	if (ret) return ret;
	state = SETIP;
	return 0;
}


/* master side message handler */
int VESlave::master_setip_gotmsg(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	if (msg->type != AsyncMessage::SUCC) {
		state = ERROR;
		return msg->type == AsyncMessage::FAIL ? msg->int1 : -EPIPE;
	}

	state = READY;
	return 0;
}


/* master side exec VE command */
int VESlave::master_exec(int ctid, const char* cmd)
{
	int ret = 0;
	if (!isinit) return -EPERM;
	if (state != READY) return -EBUSY;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;

	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::EXEC;
	m->int1 = ctid - idoffset;
	m->resize(strlen(cmd) + 1);
	memcpy(m->payload, cmd, m->payloadlen);
	ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;

	if (ret) return ret;
	state = EXEC;
	return 0;
}


/* master side message handler */
int VESlave::master_exec_gotmsg(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	if (msg->type != AsyncMessage::SUCC) {
		state = ERROR;
		return msg->type == AsyncMessage::FAIL ? msg->int1 : -EPIPE;
	}

	state = READY;
	return 0;
}


/* master side initialization */
int VESlave::master_init()
{
	int ret = 0;
	if (state == DISCONNECTED) ret = master_connect();
	if (ret) return ret;
	if (state != READY) return -EBUSY;
	if (isinit) return -EEXIST;
	if (nve <= 0 || idbase < fromid || idbase+nve-1 > toid) return -ERANGE;
	if ((int)venif.size() != nve) return -ESRCH;

	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::INIT;
	m->time = la_type;
	m->int1 = idbase - idoffset;
	m->int2 = nve;
	m->resize(nve * sizeof(int));
	for (int i = 0, *p = (int*)m->payload; i < nve; i++, p++) *p = venif[i];
	ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;

	if (ret) return ret;
	state = INIT;
	return 0;
}


/* master side message handler */
int VESlave::master_init_gotmsg(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	if (msg->type != AsyncMessage::SUCC) {
		state = ERROR;
		return msg->type == AsyncMessage::FAIL ? msg->int1 : -EPIPE;
	}

	isinit = true;
	state = READY;
	return 0;
}


/* master side clean up */
int VESlave::master_fini(bool close)
{
	int ret = 0;
	if (state == DISCONNECTED) ret = master_connect();
	if (ret) return ret;
	if (state != READY) return -EBUSY;

	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::FINI;
	m->int1 = idbase - idoffset;
	m->int2 = nve;
	m->time = close;
	ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;

	if (ret) return ret;
	state = FINI;
	return 0;
}


/* master side message handler */
int VESlave::master_fini_gotmsg(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	if (msg->type != AsyncMessage::SUCC) {
		state = ERROR;
		return msg->type == AsyncMessage::FAIL ? msg->int1 : -EPIPE;
	}

	isinit = false;
	if (msg->time) {
		int ret = master_disconnect();
		if (ret) return ret;
		state = DISCONNECTED;
	}
	else {
		state = READY;
	}
	return (int)msg->time;
}


/* master side push packets to slaves */
int VESlave::master_pushpacket()
{
	for (int i = 0; i < nve; i++)
		while (!ve[i]->recvq.empty())
		{
			EmuPacket *ppkt = ve[i]->recvq.front();
			ve[i]->recvq.pop_front();
			AsyncMessage *m = new AsyncMessage;
			m->type = AsyncMessage::PACKET;
			m->time = ppkt->timestamp;
			m->int1 = ve[i]->id - idoffset;
			m->int2 = ppkt->ifid;
			m->resize(ppkt->len);
			memcpy(m->payload, ppkt->data, ppkt->len);
			int ret = ((AsyncMessageHandler*)amh)->amh_send(m);
			delete m;
			delete ppkt;
			if (ret) return ret;
		}

	return 0;
}


/* master side test connectivity */
int VESlave::master_test()
{
	int ret = 0;
	if (state != READY) return -EBUSY;
	if (!isinit) return -ENXIO;

	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::TEST;
	m->resize(nve * sizeof(ltime_t));
	ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;

	if (ret) return ret;
	state = TEST;
	return 0;
}


/* master side message handler */
int VESlave::master_test_gotmsg(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	if (msg->type != AsyncMessage::SUCC) {
		state = ERROR;
		return msg->type == AsyncMessage::FAIL ? msg->int1 : -EPIPE;
	}

	state = READY;
	return 0;
}


/* master side emulation cycle */
int VESlave::master_cycle(bool newcycle)
{
	int ret = 0;
	if (state != READY) return -EBUSY;
	if (!isinit) return -ENXIO;

	AsyncMessage *m = new AsyncMessage;
	m->type = AsyncMessage::CYCLE;
	m->time = newbarrier ? barrier : -1;
	m->int1 = barrier - maxadv;
	m->int2 = newcycle;
	newbarrier = false;
	ret = ((AsyncMessageHandler*)amh)->amh_send(m);
	delete m;

	if (ret) return ret;
	state = CYCLE;
	return 0;
}


/* master side message handler */
int VESlave::master_cycle_gotmsg(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	if (msg->type == AsyncMessage::PACKET) {
		EmuPacket *ppkt = new EmuPacket(msg->payloadlen);
		ppkt->ifid = msg->int2;
		memcpy(ppkt->data, msg->payload, msg->payloadlen);
		ppkt->timestamp = msg->time;
		ve[msg->int1 - idbase + idoffset]->sendq.push_back(ppkt);
	}
	else if (msg->type == AsyncMessage::SUCC) {
		ndone = msg->int1;
		laerr = msg->int2;
		onedone = (ndone > 0);
		alldone = (ndone == nve);
		if (msg->payloadlen != nve * (int)sizeof(ltime_t)) return -EPIPE;
		ltime_t *p = (ltime_t*)msg->payload;
		for (int i = 0; i < nve; i++, p++) {
			ve[i]->lookahead = alldone ? *p : 0;
			ve[i]->clock = alldone ? barrier : *p;
		}
		state = READY;
	}
	else {
		state = ERROR;
		return msg->type == AsyncMessage::FAIL ? msg->int1 : -EPIPE;
	}

	return 0;
}


/* master side check if previous call has finished */
int VESlave::master_checkstatus(bool *ready)
{
	if (state == DISCONNECTED || state == READY) { *ready = true; return 0; }

	int ret = 0;
	while (1)
	{
		AsyncMessage *msg = NULL;
		if ((ret = ((AsyncMessageHandler*)amh)->amh_recv(&msg))) break;
		if (!msg) break;

		switch (state)
		{
		case SETIP:
			ret = master_setip_gotmsg(msg);
			break;
		case INIT:
			ret = master_init_gotmsg(msg);
			break;
		case TEST:
			ret = master_test_gotmsg(msg);
			break;
		case EXEC:
			ret = master_exec_gotmsg(msg);
			break;
		case CYCLE:
			ret = master_cycle_gotmsg(msg);
			break;
		case FINI:
			ret = master_fini_gotmsg(msg);
			break;
		default:
			printf("unexpected state, state=%d\n", state);
			ret = -EPIPE;
		}

		delete msg;
		if (ret) break;
	}

	if (ret < 0) return ret;
	*ready = (ret > 0) || (state == READY);
	return 0;
}


/* slave side message handler */
int VESlave::slave_gotmsg_setip(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;
	int ret, reason = 0;
	AsyncMessage *reply = NULL;

	if (msg->payloadlen > 64) gotofail(-E2BIG);
	char cmd[100];

	sprintf(cmd, "ifconfig veth%d.%d %s", msg->int1, msg->int2, msg->payload);
	if (system(cmd)) gotofail(-ENOEXEC);
	sprintf(cmd, "vzctl exec %d ifconfig eth%d %s", msg->int1, msg->int2, msg->payload);
	if (system(cmd)) gotofail(-ENOEXEC);

	reply = new AsyncMessage;
	reply->type = AsyncMessage::SUCC;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;

FAIL:
	reply = new AsyncMessage;
	reply->type = AsyncMessage::FAIL;
	reply->int1 = reason;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;
}


/* slave side message handler */
int VESlave::slave_gotmsg_exec(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;
	int ret, reason = 0;
	AsyncMessage *reply = NULL;

	if (msg->payloadlen > 160) gotofail(-E2BIG);
	char cmd[200];
	sprintf(cmd, "vzctl exec %d \"%s &\"", msg->int1, msg->payload);
	if (system(cmd)) gotofail(-ENOEXEC);

	reply = new AsyncMessage;
	reply->type = AsyncMessage::SUCC;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;

FAIL:
	reply = new AsyncMessage;
	reply->type = AsyncMessage::FAIL;
	reply->int1 = reason;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;
}


/* slave side message handler */
int VESlave::slave_gotmsg_init(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;
	int ret, reason = 0;
	AsyncMessage *reply = NULL;

	/* parse message */
	if (msg->payloadlen != msg->int2 * (int)sizeof(int)) gotofail(-EINVAL);
	idbase = msg->int1;
	nve = msg->int2;
	la_type = (int)msg->time;
	venif.resize(nve);
	for (int i = 0, *p = (int*)msg->payload; i < nve; i++, p++) venif[i] = *p;

	/* init VEs */
	envid_t idlist[EMU_MAX_VE];
	struct timeval tv;
	int n;

	if (isinit) gotofail(-EEXIST);
	if (nve <= 0) gotofail(-EINVAL);
	tv.tv_sec = tv.tv_usec = 0;

	/* validate vtime VEs */
	for (int i = 0; i < nve; i++)
		if (vtime_setenable(idbase+i, 0)) gotofail(-EINTR);
	if (vtime_getvtimeve(&n, idlist)) gotofail(-EINTR);
	if (n != 0) gotofail(-ESRCH);

	/* enable vtime switches */
	for (int i = 0; i < nve; i++) {
		if (vtime_setenable(idbase+i, 1)) gotofail(-EINTR);
		if (vtime_setclock(idbase+i, &tv)) gotofail(-EINTR);
	}

	/* init VE mapping */
	isinit = true;
	vemap.resize(nve);
	ve.resize(nve);
	done.resize(nve);
	pktcount.resize(nve);
	vtime_getvtimeve(&n, idlist);
	for (int i = 0; i < nve; i++) vemap[idlist[i]-idbase] = i;

	/* init VEHandle */
	for (int i = 0; i < nve; i++) {
		VEHandle *p = new VEHandle(idbase + i, la_type, venif[i]);
		if (!p) gotofail(-ENOMEM);
		ve[vemap[i]] = p;
	}

	/* open pcap handles */
	for (int i = 0; i < nve; i++) {
		VEHandle *p = ve[vemap[i]];
		char str[20];
		char errbuf[PCAP_ERRBUF_SIZE];
		for (int j = 0; j < p->nif; j++) {
			sprintf(str, "veth%d.%d", p->id, j);
			p->pcap[j] = (void*)pcap_open_live(str, BUFSIZ, 0, 1000, errbuf);
			if (!p->pcap[j]) gotofail(-ENODEV);
			if (pcap_setdirection((pcap_t*)p->pcap[j], PCAP_D_IN)) gotofail(-EIO);
			if (pcap_setnonblock((pcap_t*)p->pcap[j], 1, errbuf)) gotofail(-EIO);
		}
	}

#ifdef TIME_STAT
	timestat_counter = 0;
	gettimeofday(&lastprint, NULL);
	lastvtime = 0;
#endif

#ifdef SLAVE_LOG
	pfslave = fopen("slave.log", "a");
	if (!pfslave) gotofail(-EACCES);
	fprintf(pfslave, "========================================\n");
	if (la_type) for (int i = 0; i < nve; i++) ((Lookahead*)ve[i]->la)->set_pflog(pfslave);
#endif

	/* reply message */
	reply = new AsyncMessage;
	reply->type = AsyncMessage::SUCC;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;

FAIL:
	reply = new AsyncMessage;
	reply->type = AsyncMessage::FAIL;
	reply->int1 = reason;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;
}


/* slave side message handler */
int VESlave::slave_gotmsg_fini(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;
	AsyncMessage *reply = NULL;

	idbase = msg->int1;
	nve = msg->int2;

	/* disable virtual time switches */
	for (int i = 0; i < nve; i++)
		vtime_setenable(idbase+i, 0);

	if (isinit) 
	{
		/* delete VEHandle */
		for (int i = 0; i < nve; i++) if (ve[i]) delete ve[i];
		vemap.clear();
		ve.clear();

		isinit = false;
	}

#ifdef SLAVE_LOG
	if (pfslave) {
		fprintf(pfslave, "========================================\n\n");
		fclose(pfslave);
		pfslave = NULL;
	}
#endif

	/* reply message */
	reply = new AsyncMessage;
	reply->type = AsyncMessage::SUCC;
	reply->time = msg->time;
	int ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	if (ret) return ret;

	/* close connection */
	return (int)msg->time;
}


/* slave side message handler */
int VESlave::slave_gotmsg_close(void *m)
{
	/* close connection */
	return 1;
}


/* slave side message handler */
int VESlave::slave_gotmsg_packet(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;

	EmuPacket *ppkt = new EmuPacket(msg->payloadlen);
	ppkt->ifid = msg->int2;
	memcpy(ppkt->data, msg->payload, msg->payloadlen);
	ppkt->timestamp = msg->time;
	ve[vemap[msg->int1 - idbase]]->recvq.push_back(ppkt);

	return 0;
}


/* slave side message handler */
int VESlave::slave_gotmsg_test(void *m)
{
	/* reply message */
	AsyncMessage *reply = new AsyncMessage;
	reply->type = AsyncMessage::SUCC;
	reply->resize(nve * sizeof(ltime_t));
	int ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;
}


/* for pcap_dispatch */
static void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{
	VEHandle *pve = (VEHandle*)args;
	EmuPacket *ppkt = new EmuPacket(header->len);
	ppkt->ifid = pve->nowif;
	memcpy(ppkt->data, packet, header->len);
	ppkt->timestamp = (header->ts.tv_sec%1000)*1000000 + header->ts.tv_usec;
	if (ppkt->timestamp > pve->get_clock()) ppkt->timestamp = pve->get_clock();
	if (ppkt->timestamp < pve->get_lastc()) ppkt->timestamp = pve->get_lastc();
	pve->sendq.push_back(ppkt);
	if (debug) printf("VE%d sent packet, time=%ld size=%d\n",
		pve->get_id(), ppkt->timestamp, ppkt->len);
}


/* lookahead prediction thread function, for multithread */
void* la_postcycle_thread(void *arg)
{
	int tid = (long)arg;
	for (int i = tid; i < pslave->nve; i += pslave->nthread)
		if (pslave->ve[i]->la)
			((Lookahead*)pslave->ve[i]->la)->post_cycle(pslave->barrier);
		else pslave->ve[i]->lookahead = 0;
	return NULL;
}


/* slave side message handler */
int VESlave::slave_gotmsg_cycle(void *m)
{
	AsyncMessage *msg = (AsyncMessage*)m;
	int ret, reason = 0;
	AsyncMessage *reply = NULL;
	ltime_t *p;

	struct timeval tv;
	int rllist[EMU_MAX_VE];
	vtime_vestatus_t stlist[EMU_MAX_VE];
	struct vtime_trap trlist[VTIME_SZ_TRAPLIST];
	int rtlist[VTIME_SZ_TRAPLIST];
	int count;
	bool again, allidle, newcycle;

	if (debug) printf("----- cycle call -----\n");

	/* parse message */
	if (msg->time >= 0) {
		barrier = msg->time;
		tvbarrier.tv_sec = barrier / 1000000;
		tvbarrier.tv_usec = barrier % 1000000;
		newbarrier = true;
	}
	maxadv = barrier - msg->int1;
	newcycle = msg->int2;

	if (!newcycle) goto STEP2;

	/* step 1: lookahead pre_cycle for all VEs */
	if (la_type) {
		for (int i = 0; i < nve; i++)
			if (ve[i]->la) ((Lookahead*)ve[i]->la)->pre_cycle(barrier);
	}

	for (int i = 0; i < nve; i++) { done[i] = false; pktcount[i] = 0; }
	ndone = 0;
	
STEP2:
	/* step 2: set the new barrier to all VEs */
	if (newbarrier) {
		if (debug) printf("new barrier for all VEs: %ld\n", barrier);
		for (int i = 0; i < nve; i++)
			if (vtime_setbarrier(ve[i]->id, &tvbarrier)) gotofail(-EINTR);
		newbarrier = false;
	}

STEP3:
	/* step 3: push due recv packets & due timers */
	for (int i = 0; i < nve; i++) if (!done[i])
	{
		/* push recv packets */
		deque<EmuPacket*> *pq1 = &(ve[i]->recvq);
		while (!pq1->empty() && pq1->front()->timestamp <= ve[i]->clock) {
			EmuPacket *ppkt = pq1->front();
			pq1->pop_front();
			ve[i]->idle = false;
			if (ppkt->ifid >= 0 && ppkt->ifid < ve[i]->nif)
			{
				pcap_inject((pcap_t*)ve[i]->pcap[ppkt->ifid],
					    ppkt->data, ppkt->len);
#ifdef SLAVE_LOG
				fprintf(pfslave, "%ld.%06ld\tve%d\trecv\t%d\n",
					ve[i]->clock/1000000, ve[i]->clock%1000000,
					ve[i]->id, ppkt->len);
#endif
				if (debug) printf("VE%d recv packet, time=%ld size=%d\n",
					ve[i]->id, ve[i]->clock, ppkt->len);
			}
			else {
				fprintf(stderr, "simctrl warning: "
					"ifid=%d, nif=%d, packet dropped\n",
					ppkt->ifid, ve[i]->nif);
			}
			delete ppkt;
		}
		/* push due timers */
		priority_queue<EmuTimer> *pq2 = &(ve[i]->timerq);
		while (!pq2->empty() && pq2->top().due <= ve[i]->clock) {
			EmuTimer timer = pq2->top();
			pq2->pop();
			ve[i]->idle = false;
			if (timer.pid) vtime_firetimer(timer.pid);
			if (debug) printf("VE%d timer due, time=%ld\n", ve[i]->id, timer.due);
			if (timer.repeat) {
				timer.due = ve[i]->clock + timer.repeat;
				pq2->push(timer);
			}
		}
	}
	
	/* step 4: release selected VEs */
	if (debug) printf("maxadv=%ld barrier=%ld\n", maxadv, barrier);
	again = false;
	allidle = true;
	for (int i = 0; i < nve; i++) {
		rllist[i] = (!done[i])
			&& (!ve[i]->idle || ve[i]->idlecount > MAX_IDLE_COUNT)
			&& (ve[i]->clock < maxadv - min(ve[i]->offset, 0L));
		if (!done[i] && ve[i]->clock < maxadv - min(ve[i]->offset, 0L)) again = true;
		if (debug) printf("VE%d clock=%ld rllist=%d done=%d "
				"idle=%d idlecount=%d offset=%ld\n",
				ve[i]->id, ve[i]->clock, (int)rllist[i],
				(int)done[i], (int)ve[i]->idle,
				ve[i]->idlecount, ve[i]->offset);
		if (rllist[i]) { allidle = false; ve[i]->idlecount = 0; }
	}
	if (allidle) goto STEP9;
	if (vtime_releaseve(nve, rllist)) return -EINTR;

	/* step 5: wait until all VEs have stopped */
	count = 0;
	while (1)
	{
		int n;
		bool ok = true;
		if (vtime_getvestatus(&n, stlist)) return -EINTR;
		if (n != nve) return -ESRCH;
		for (int i = 0; i < nve; i++) {
			if (0 && debug && count > 20) {
				printf("VEs still not done, stlist =");
				for (int i = 0; i < nve; i++) printf(" %d", stlist[i]);
				printf("\n");
			}
			if (count > 100 && stlist[i] == VTIME_READY)
				stlist[i] = VTIME_SUSPENDED;
			if (stlist[i] != VTIME_SUSPENDED && stlist[i] != VTIME_IDLE) {
				ok = false; /* some VEs still running */
				sched_yield();
				break;
			}
		}
		if (ok) break;
		count++;
	}
	if (debug) {
		printf("VEs have stopped, stlist =");
		for (int i = 0; i < nve; i++) printf(" %d", stlist[i]);
		printf("\n");
	}

	/* step 6: collect sent packets */
	for (int i = 0; i < nve; i++) if (rllist[i])
	{
		for (int j = 0; j < ve[i]->nif; j++)
		{
			ve[i]->nowif = j;
			while (pcap_dispatch((pcap_t*)ve[i]->pcap[j],
					-1, got_packet, (u_char*)ve[i]) > 0)
				stlist[i] = VTIME_SUSPENDED;
			while (pktcount[i] < (int)ve[i]->sendq.size())
			{
				EmuPacket *ppkt = ve[i]->sendq[pktcount[i]++];
#ifdef SLAVE_LOG
				fprintf(pfslave, "%ld.%06ld\tve%d\tsend\t%d\n",
					ppkt->timestamp/1000000, ppkt->timestamp%1000000,
					ve[i]->id, ppkt->len);
#endif
				AsyncMessage *pktmsg= new AsyncMessage;
				pktmsg->type = AsyncMessage::PACKET;
				pktmsg->time = ppkt->timestamp;
				pktmsg->int1 = ve[i]->id;
				pktmsg->int2 = ve[i]->nowif;
				pktmsg->resize(ppkt->len);
				memcpy(pktmsg->payload, ppkt->data, ppkt->len);
				ret = ((AsyncMessageHandler*)amh)->amh_send(pktmsg);
				delete pktmsg;
				if (ret) return ret;
			}
		}
	}

	/* step 7: collect timers */
	for (int i = 0; i < nve; i++) if (rllist[i])
	{
		int n;
		if (vtime_gettraplist(ve[i]->id, &n, trlist)) return -EINTR;
		for (int j = 0; j < n; j++)
			if (trlist[j].type == VTIME_TRAPTIMER &&
			    trlist[j].data[0] > 0 && trlist[j].data[0] < 1000000) 
			{
				EmuTimer timer;
				timer.pid = trlist[j].pid;
				timer.due = (trlist[j].data[0] +
					     trlist[j].time.tv_sec) * 1000000 +
					trlist[j].data[1] + trlist[j].time.tv_usec;
				timer.repeat = trlist[j].data[2]*1000000 +
					trlist[j].data[3];
				ve[i]->timerq.push(timer);
			}
			else if (trlist[j].type == VTIME_TRAPSLEEP &&
				trlist[j].data[0] > 0 && trlist[j].data[0] < 1000000)
			{
				EmuTimer timer;
				timer.pid = 0;
				timer.due = trlist[j].data[0]*1000000 + trlist[j].data[1];
				timer.repeat = 0;
				ve[i]->timerq.push(timer);
			}
		/* release all traps */
		for (int j = 0; j < n; j++) rtlist[j] = 1;
		if (vtime_releasetrap(ve[i]->id, n, rtlist)) return -EINTR;
	}

	/* step 8: update VE clocks */
	for (int i = 0; i < nve; i++) if (rllist[i]) {
		if (vtime_getclock(ve[i]->id, &tv)) return -EINTR;
		ve[i]->lastclock = ve[i]->clock;
		ve[i]->clock = tv.tv_sec*1000000 + tv.tv_usec;
		if (debug) printf("VE%d clock=%ld lastc=%ld run=%ld\n", ve[i]->id,
				ve[i]->clock, ve[i]->lastclock, ve[i]->clock-ve[i]->lastclock);
		if (stlist[i] != VTIME_IDLE) 
			ve[i]->avgrun = (9 * ve[i]->avgrun +
					(ve[i]->clock - ve[i]->lastclock)) / 10;
	}

STEP9:
	/* step 9: adjust VEs virtual clocks */
	alldone = true;
	for (int i = 0; i < nve; i++) if (!done[i])
	{
		ltime_t adjbarrier = barrier - ve[i]->offset - ve[i]->avgrun/2;
		if (debug) printf("VE%d offset=%ld avgrun=%ld adjbarrier=%ld\n",
				ve[i]->id, ve[i]->offset, ve[i]->avgrun, adjbarrier);

		/* quick advance VE when idle */
		if (stlist[i] == VTIME_IDLE)
		{
			ve[i]->idle = true;
			ve[i]->idlecount++;
			ltime_t next = min(maxadv, ve[i]->clock + VTIME_VTIMESLICE), ts;
			if (!ve[i]->recvq.empty() &&
			    (ts = ve[i]->recvq.front()->timestamp) < adjbarrier)
				if (next < 0 || ts < next) next = ts;
			if (!ve[i]->timerq.empty() &&
			    (ts = ve[i]->timerq.top().due) < adjbarrier)
				if (next < 0 || ts < next) next = ts;
			if (next > ve[i]->clock) {
				struct timeval tvnext;
				tvnext.tv_sec = next / 1000000;
				tvnext.tv_usec = next % 1000000;
				if (vtime_setclock(ve[i]->id, &tvnext)) return -EINTR;
				if (debug) printf("VE%d idle advance to %ld\n",
						ve[i]->id, next);
				ve[i]->offset = 0;
				ve[i]->lastclock = ve[i]->clock = next;
			}
		}
		else {
			ve[i]->idle = false;
		}
			
		/* VE has done current cycle */
		if (ve[i]->clock >= adjbarrier)
		{
			if (debug) printf("VE%d reached barrier\n", ve[i]->id);
			if (vtime_setclock(ve[i]->id, &tvbarrier)) return -EINTR;
			if (stlist[i] != VTIME_IDLE) ve[i]->offset += ve[i]->clock - barrier;
				else ve[i]->offset = 0;
			ve[i]->lastclock = ve[i]->clock = barrier;
			done[i] = true;
			onedone = true;
			ndone++;
		}
		else
			alldone = false;
	}

	if (debug && !debug_nopause) {
		printf("press enter to continue\n");
		char line[100];
		fgets(line, 100, stdin);
		if (strcmp(line, "exit") == 0) debug_nopause = true;
	}

	if (!alldone && again) goto STEP3;
	if (!alldone) goto EXIT;

	/* step 10: lookahead post_cycle for all VEs */
	if (la_type) {
		if (nthread == 1)
		{
			for (int i = 0; i < nve; i++) {
				if (ve[i]->la) ((Lookahead*)ve[i]->la)->post_cycle(barrier);
				else ve[i]->lookahead = 0;
			}
		}
		else
		{
			/* use multi-thread to accelerate */
			vector<pthread_t> tid(nthread);
			void *status;
			for (int i = 0; i < nthread; i++)
				if (pthread_create(&tid[i], NULL, la_postcycle_thread, (void*)i))
					gotofail(-ECHILD);
			for (int i = 0; i < nthread; i++)
				if (pthread_join(tid[i], &status)) gotofail(-ECHILD);
		}
#ifdef TIME_STAT
		ltime_t minla = -1, totalla = 0;
		for (int i = 0; i < nve; i++) {
			if (minla < 0 || ve[i]->lookahead < minla) minla = ve[i]->lookahead;
			totalla += ve[i]->lookahead;
		}
		if (!timestat_counter) timestat_update(avgminla, minla);
		if (!timestat_counter) timestat_update(avgla, totalla / nve);
#endif
	}

	/* empty sendq */
	for (int i = 0; i < nve; i++)
		while (!ve[i]->sendq.empty()) {
			EmuPacket *ppkt = ve[i]->sendq.front();
			ve[i]->sendq.pop_front();
			delete ppkt;
		}

#ifdef TIME_STAT
	timestat_counter = (timestat_counter + 1) % timestat_interval;
	if (!timestat_counter)
	{
		gettimeofday(&tstv2, NULL);
		int elapse = VTIME_CLOCKDIFFU(tstv2, lastprint);
		if (elapse > 1000000)
		{
			double speed = (double)(barrier - lastvtime) / elapse;
			lastprint = tstv2;
			lastvtime = barrier;
			fprintf(stderr, "vtime=%d.%03d speed=%.3f",
				(int)(barrier/1000000), (int)(barrier%1000000/1000), speed);
			if (la_type) {
				double err = 0.0;
				for (int i = 0; i < nve; i++)
					err += ((Lookahead*)ve[i]->la)->get_error();
				err /= nve;
				laerr = (int)(err * 10000);
				fprintf(stderr, " err=%.0f%% avgla=%.0f avgminla=%.0f",
					err*100, avgla, avgminla);
			}
			fprintf(stderr, "\n");
		}
	}
#endif

EXIT:
	if (debug) printf("----- cycle done -----\n");

	/* reply message */
	reply = new AsyncMessage;
	reply->type = AsyncMessage::SUCC;
	reply->int1 = ndone;
	reply->int2 = laerr;
	reply->resize(nve * sizeof(ltime_t));
	p = (ltime_t*) reply->payload;
	for (int i = 0; i < nve; i++, p++)
		*p = alldone ? ve[vemap[i]]->lookahead : ve[vemap[i]]->clock;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;

FAIL:
	reply = new AsyncMessage;
	reply->type = AsyncMessage::FAIL;
	reply->int1 = reason;
	ret = ((AsyncMessageHandler*)amh)->amh_send(reply);
	delete reply;
	return ret;
}


/* slave side main function */
int VESlave::slave_mainloop()
{ 
	int ret;

	envid_t idlist[EMU_MAX_VE];
	int n;
	if (vtime_getvtimeve(&n, idlist)) {
		printf("vtime openvz kernel not detected!\n");
		return -EPERM;
	}

	AsyncMessageHandler *amh = new AsyncMessageHandler;
	if (!amh) return -ENOMEM;
	this->amh = amh;

	while (1)
	{
		printf("waiting for master\n");
		if ((ret = amh->amh_connect())) return ret;
		printf("master connected\n");
		while (1)
		{
			AsyncMessage *msg = NULL;
			if ((ret = amh->amh_recv(&msg))) return ret;
			if (!msg) { sched_yield(); continue; }

			switch (msg->type)
			{
			case AsyncMessage::SETIP:
				ret = slave_gotmsg_setip(msg);
				break;
			case AsyncMessage::INIT:
				ret = slave_gotmsg_init(msg);
				break;
			case AsyncMessage::FINI:
				ret = slave_gotmsg_fini(msg);
				break;
			case AsyncMessage::CLOSE:
				ret = slave_gotmsg_close(msg);
				break;
			case AsyncMessage::PACKET:
				ret = slave_gotmsg_packet(msg);
				break;
			case AsyncMessage::TEST:
				ret = slave_gotmsg_test(msg);
				break;
			case AsyncMessage::EXEC:
				ret = slave_gotmsg_exec(msg);
				break;
			case AsyncMessage::CYCLE:
				ret = slave_gotmsg_cycle(msg);
				break;
			default:
				printf("unexpected message, type=%d\n", msg->type);
				ret = -EPIPE;
			}

			delete msg;
			if (ret < 0) return ret;
			if (ret > 0) break;
		}
		if ((ret = amh->amh_close())) return ret;
		printf("master disconnected\n\n");
	}

	delete amh;
	this->amh = NULL;
	return 0;
}


/***************************/
/* SimCtrl class functions */
/***************************/

/* constructor */
DSimCtrl::DSimCtrl() : SimCtrl()
{
	isinit = false;
	idbase = 0;
	nve = 0;
	nslave = 0;
	barrier = 0;
	lastbarrier = 0;
#ifdef TIME_STAT
	firsttime = true;
	timestat_counter = 0;
#endif
}


/* destructor */
DSimCtrl::~DSimCtrl()
{
	if (isinit) emu_done();
	else for (int i = 0; i < nslave; i++) veslave[i]->master_disconnect();
	for (int i = 0; i < nslave; i++) delete veslave[i];
}


/* set VE boundary for emulation, return 0 iff succeed */
int DSimCtrl::set_ve_boundary(int idbase, int nve)
{
	if (isinit) return -EPERM;
	if (idbase <= 0) return -EINVAL;
	if (nve <= 0) return -EINVAL;
	venif.resize(nve);
	for (int i = 0; i < nve; i++) venif[i] = 1;

	/* read VE slave configuration file if we haven't */
	if (!nslave) {
		FILE *pf = fopen("vemaster.cfg", "r");
		if (!pf) return -EBADF;
		fscanf(pf, "%d\n", &nslave);
		veslave.resize(nslave);
		for (int i = 0; i < nslave; i++) {
			veslave[i] = new VESlave;
			veslave[i]->addr = new char[100];
			if (fscanf(pf, "%s %d %d %d\n", veslave[i]->addr, &veslave[i]->fromid,
				&veslave[i]->toid, &veslave[i]->idoffset) != 4)
				return -ENOENT;
		}
		fclose(pf);
	}
	if (!nslave) return -ENODEV;

	/* prepare findslave mapping */
	findslave.resize(nve);
	for (int i = 0; i < nve; i++) findslave[i] = -1;
	int idto = idbase + nve - 1; 
	for (int i = 0; i < nslave; i++) {
		veslave[i]->selected = veslave[i]->fromid <= idto && veslave[i]->toid >= idbase;
		if (!veslave[i]->selected) continue;
		veslave[i]->idbase = max(veslave[i]->fromid, idbase);
		veslave[i]->nve = min(veslave[i]->toid, idto) - veslave[i]->idbase + 1;
		for (int j = 0; j < veslave[i]->nve; j++)
			findslave[veslave[i]->idbase+j - idbase] = i;
	}
	for (int i = 0; i < nve; i++) if (findslave[i] < 0) return -ERANGE;

	this->idbase = idbase;
	this->nve = nve;
	return 0;
}


/* get a VEHandle by ID */
VEHandle* DSimCtrl::get_vehandle(int ctid)
{
	if (!isinit) return NULL;
	if (ctid < idbase || ctid >= idbase+nve) return NULL;
	return ve[ctid-idbase];
}


/* set the min delay that an outgoing packet of a VE may affect other VEs, return 0 iff succeed */
int DSimCtrl::set_ve_mindelay(int ctid, ltime_t delay)
{
	if (!isinit) return -ENXIO;
	if (la_type) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	if (delay < 0) return -EINVAL;
	ve[ctid-idbase]->mindelay = delay;
	if (debug) printf("set_ve_mindelay ctid=%d delay=%ld\n", ctid, delay);
	return 0;
}


/* get the min delay of a VE */
ltime_t DSimCtrl::get_ve_mindelay(int ctid)
{
	if (!isinit) return -ENXIO;
	if (la_type) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	return ve[ctid-idbase]->mindelay;
}


/* set the number of interfaces of a VE, return 0 iff succeed */
int DSimCtrl::set_ve_nif(int ctid, int nif)
{
	if (isinit) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve || nif < 0) return -ERANGE;
	venif[ctid-idbase] = nif;
	return 0;
}


/* get the number of interfaces of a VE */
int DSimCtrl::get_ve_nif(int ctid)
{
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	return venif[ctid-idbase];
}


/* set the IP address of an interface, return 0 iff succeed */
int DSimCtrl::set_ve_ifip(int ctid, int ifid, const char* ip)
{
	if (isinit) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	if (ifid < 0 || ifid >= venif[ctid-idbase]) return -ERANGE;

	VESlave *sl = veslave[findslave[ctid-idbase]];
	int ret = sl->master_setip(ctid, ifid, ip);
	if (ret) return ret;
	while (1) {
		bool ready;
		ret = sl->master_checkstatus(&ready);
		if (ret || ready) break;
		sched_yield();
	}
	if (ret) return ret;

	return 0;
}


/* set the IP address of an interface, return 0 iff succeed */
int DSimCtrl::exec_ve_command(int ctid, const char *cmd)
{
	if (isinit) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;

	VESlave *sl = veslave[findslave[ctid-idbase]];
	int ret = sl->master_exec(ctid, cmd);
	if (ret) return ret;
	while (1) {
		bool ready;
		ret = sl->master_checkstatus(&ready);
		if (ret || ready) break;
		sched_yield();
	}
	if (ret) return ret;

	return 0;
}


/* emulation initialization: call once before emulation starts, return 0 iff succeed */
int DSimCtrl::emu_init(int la_type)
{
	if (isinit) return -EEXIST;
	if (nve <= 0) return -EINVAL;

	/* prepare VE slaves */
	for (int i = 0; i < nslave; i++) if (veslave[i]->selected) {
		VESlave *sl = veslave[i];
		sl->la_type = la_type;
		sl->laerr = 0;
		sl->venif.resize(sl->nve);
		for (int j = 0; j < sl->nve; j++) sl->venif[j] = venif[sl->idbase+j-idbase];
	}

	/* init for all VE slaves */
	int ret;
	for (int i = 0; i < nslave; i++) if (veslave[i]->selected) {
		ret = veslave[i]->master_init();
		if (ret) return ret;
	}

	/* wait until VE slaves finished */
	bool allready = false; 
	while (!allready)
	{
		bool ready;
		allready = true;
		for (int i = 0; i < nslave && !ret && allready; i++)
			if (veslave[i]->selected) {
				ret = veslave[i]->master_checkstatus(&ready);
				allready = allready && ready;
				if (ret) return ret;
			}
		if (!allready) sched_yield();
	}

#ifdef TIME_STAT
	struct timeval tv0, tv1;
	gettimeofday(&tv0, NULL);
	const int time = 1000;

	for (int i = 0; i < time; i++)
	{
		for (int i = 0; i < nslave; i++) if (veslave[i]->selected) {
			ret = veslave[i]->master_test();
			if (ret) return ret;
		}

		allready = false; 
		while (!allready)
		{
			bool ready;
			allready = true;
			for (int i = 0; i < nslave && !ret && allready; i++)
				if (veslave[i]->selected) {
					ret = veslave[i]->master_checkstatus(&ready);
					allready = allready && ready;
				}
			if (ret) return ret;
			if (!allready) sched_yield();
		}
	}

	gettimeofday(&tv1, NULL);
	syncoverhead = 1.0 * VTIME_CLOCKDIFFU(tv1, tv0) / time;
	fprintf(stderr, "master-slave sync overhead=%.0f\n", syncoverhead);
#endif

	/* init VEhandle */
	ve.resize(nve);
	for (int i = 0; i < nslave; i++) if (veslave[i]->selected)
		veslave[i]->ve.resize(veslave[i]->nve);
	for (int i = 0; i < nve; i++) {
		ve[i] = new VEHandle(idbase + i, LOOKAHEAD_NONE, venif[i]);
		if (!ve[i]) return -ENOMEM;
		VESlave *sl = veslave[findslave[i]];
		sl->ve[idbase + i - sl->idbase] = ve[i];
	}

#ifdef TIME_STAT
	firsttime = true;
	if (gtoddelay < 0) {
		struct timeval tv0, tv1;
		const int times = 100;
		gettimeofday(&tv0, NULL);
		for (int i = 0; i < times; i++) gettimeofday(&tv1, NULL);
		gtoddelay = VTIME_CLOCKDIFFU(tv1, tv0) / (double)times;
	}
#endif

	this->la_type = la_type;
	isinit = true;
	return 0;
}


/* emulation clean up: call once after emulation ends, return 0 iff succeed */
int DSimCtrl::emu_done()
{
	if (!isinit) return -ENXIO;

	/* fini for all VE slaves */
	int ret = 0;
	ret = emu_reset(true);

	/* delete VEHandle */
	for (int i = 0; i < nve; i++) if (ve[i]) delete ve[i];
	ve.clear();
	for (int i = 0; i < nslave; i++) if (veslave[i]->selected)
		veslave[i]->ve.clear();

	isinit = false;
	return ret;
}


/* emulation reset */
int DSimCtrl::emu_reset(bool close)
{
	/* fini for all VE slaves */
	int ret = 0;
	for (int i = 0; i < nslave && !ret; i++) if (veslave[i]->selected)
		ret = veslave[i]->master_fini(close);
	if (ret) return ret;

	/* wait until VE slaves finished */
	bool allready = false; 
	while (!allready)
	{
		bool ready;
		allready = true;
		for (int i = 0; i < nslave && !ret && allready; i++)
			if (veslave[i]->selected) {
				ret = veslave[i]->master_checkstatus(&ready);
				allready = allready && ready;
			}
		if (ret) return ret;
		if (!allready) sched_yield();
	}

	isinit = false;
	return 0;
}


/* emulation cycle: advance all VEs to the barrier, return 0 iff succeed */
int DSimCtrl::emu_cycle(ltime_t barrier)
{
	if (!isinit) return -ENXIO;
	if (barrier <= this->barrier) return -EINVAL;

	if (debug) printf("########## start of cycle ##########\n");
	if (debug) printf("barrier=%ld lastbarrier=%ld\n", barrier, lastbarrier);

#ifdef TIME_STAT
	timestat_counter = (timestat_counter + 1) % timestat_interval;
	if (timestat_counter == 1 || timestat_interval == 1) {
		if (!firsttime) {
			gettimeofday(&tstv2, NULL);
			timestat_update(simtime, VTIME_CLOCKDIFFU(tstv2, tstv1)-gtoddelay);
		}
		else {
			gettimeofday(&lastprint, NULL);
			lastvtime = 0;
			firsttime = false;
		}
	}
	if (!timestat_counter) gettimeofday(&tstv1, NULL);

#endif

	int ret, allready;
	bool newcycle = true;

	/* precycle for all VE slaves */
	for (int i = 0; i < nslave; i++) if (veslave[i]->selected) {
		veslave[i]->barrier = barrier;
		veslave[i]->newbarrier = true;
		veslave[i]->alldone = false;
		ret = veslave[i]->master_pushpacket();
		if (ret) return ret;
	}

	if (debug) {
		for (int i = 0; i < nve; i++) if (!ve[i]->recvq.empty())
			printf("error: VE %d recvq not empty\n", idbase+i);
	}

	/* cycle for all VE slaves */
	bool alldone = false, onedone = false;
	vector<bool> sent(nve);
	for (int i = 0; i < nve; i++) sent[i] = false;
	int stepcount = 0;
	while (!alldone)
	{
		/* calculate maxadv */
		stepcount++;
		ltime_t maxadv = barrier;
		for (int i = 0; !la_type && i < nve; i++) {
			ltime_t temp = ve[i]->clock +
				       max(ve[i]->mindelay, (ltime_t)VTIME_VTIMESLICE);
			if (temp < maxadv) maxadv = temp;
		}

		if (debug) printf("release VE slaves, barrier=%ld maxadv=%ld\n", barrier, maxadv);

		/* release VE slaves */
		for (int i = 0; i < nslave; i++)
			if (veslave[i]->selected && !veslave[i]->alldone) {
				veslave[i]->maxadv = maxadv;
				ret = veslave[i]->master_cycle(newcycle);
				if (ret) return ret;
		}
		newcycle = false;

		allready = false; 
		while (!allready)
		{
			bool ready;
			allready = true;
			for (int i = 0; i < nslave && !ret && allready; i++)
				if (veslave[i]->selected) {
					ret = veslave[i]->master_checkstatus(&ready);
					allready = allready && ready;
				}
			if (ret) return ret;
			if (!allready) sched_yield();
		}

		if (debug) printf("VE slaves report done\n");

		/* update VE slave status */
		alldone = true;
		for (int i = 0; i < nslave; i++) if (veslave[i]->selected) {
			alldone = alldone && veslave[i]->alldone;
			onedone = onedone || veslave[i]->onedone;
		}

		/* shrink barrier */
		if (!la_type && !onedone)
		{
			bool newbarrier = false;
			for (int i = 0; i < nve; i++) if (!sent[i] && !ve[i]->sendq.empty())
			{
				sent[i] = true;
				ltime_t temp = ve[i]->sendq.front()->timestamp;
				temp += max(ve[i]->mindelay, (ltime_t)VTIME_VTIMESLICE);
				if (temp < barrier) {
					barrier = temp;
					newbarrier = true;
					if (debug) printf("barrier shrinked to %ld\n", barrier);
				}
			}
			if (newbarrier)
				for (int i = 0; i < nslave; i++) if (veslave[i]->selected) {
					veslave[i]->barrier = barrier;
					veslave[i]->newbarrier = true;
				}
		}

	} /* end of while (!alldone) */

#ifdef TIME_STAT
	if (!timestat_counter) {
		gettimeofday(&tstv2, NULL);
		timestat_update(emutime, VTIME_CLOCKDIFFU(tstv2, tstv1)-gtoddelay);
		tstv1 = tstv2;
	}
#endif

	lastbarrier = this->barrier;
	this->barrier = barrier;

#ifdef TIME_STAT
	if (!timestat_counter) {
		timestat_update(esw, barrier - lastbarrier);
		timestat_update(mswin, (barrier - lastbarrier) * 1.0 / stepcount);
	}

	if (!timestat_counter && la_type)
	{
		ltime_t minla = -1, totalla = 0;
		for (int i = 0; i < nve; i++) {
			if (minla < 0 || ve[i]->lookahead < minla) minla = ve[i]->lookahead;
			totalla += ve[i]->lookahead;
		}
		if (!timestat_counter) timestat_update(avgminla, minla);
		if (!timestat_counter) timestat_update(avgla, totalla / nve);
	}
		
	if (!timestat_counter)
	{
		gettimeofday(&tstv2, NULL);
		int elapse = VTIME_CLOCKDIFFU(tstv2, lastprint);
		if (elapse > 1000000)
		{
			double speed = (double)(barrier - lastvtime) / elapse;
			lastprint = tstv2;
			lastvtime = barrier;
			double alltime = simtime + emutime;
			char lastr1[100], lastr2[100], lastr3[100];
			if (!la_type) {
				strcpy(lastr1, "n/a");
				strcpy(lastr2, "n/a");
				strcpy(lastr3, "n/a");
			}
			else {
				int totallaerr = 0;
				for (int i = 0; i < nslave; i++) if (veslave[i]->selected)
					totallaerr += veslave[i]->laerr * veslave[i]->nve;
				sprintf(lastr1, "%.0f%%", 1.0 * totallaerr / nve / 100);
				sprintf(lastr2, "%.0f", avgla);
				sprintf(lastr3, "%.0f", avgminla);
			}
			double overhead = syncoverhead / (emutime / (esw / mswin)) * 100;
			fprintf(stderr, "vtime=%d.%03d speed=%.3f sim=%.0f%% "
				"emu=%.0f%% mswin=%.0f dcost=%.0f%% "
				"avgla=%s avgminla=%s laerr=%s\n",
				(int)(barrier/1000000), (int)(barrier%1000000/1000), speed,
				simtime*100/alltime, emutime*100/alltime,
				mswin, overhead, lastr2, lastr3, lastr1);
		}
	}
#endif
	if (debug) printf("########## end of cycle ##########\n");

	return 0;
}

