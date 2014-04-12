/** \file ssimctrl.cc
 * \brief source file for the single-machine VE controller
 *
 * authors : Yuhao Zheng
 *           Dong (Kevin) Jin
 */

#include <simctrl/vtime_syscall.h>
#include <simctrl/lookahead.h>
#include <s3f.h>
#include <s3fnet/src/net/net.h>
#include <pcap.h>
#include <errno.h>

#define EMU_MAX_VE 1000
#define MAX_IDLE_COUNT 1
//#define LOGGING
#define TIME_STAT

#ifdef TIME_STAT
static double nstime = 1, vctime1 = 1, vctime2 = 1, vetime = 1,
	      latime1 = 1, latime2 = 1, avgesw = 1;
static struct timeval tv1, tv2, lastprint;
static ltime_t lastvtime;
static bool firsttime = true;
static const int timestat_interval = 1;
static int timestat_counter = 0;
#endif

#ifdef LOGGING
static FILE *pf = NULL;
#endif

static bool debug_nopause = true;
static const bool debug = false;


/* SimCtrl constructor */
SSimCtrl::SSimCtrl() : SimCtrl()
{
	isinit = false;
	idbase = 0;
	nve = 0;
	barrier = 0;
	lastbarrier = 0;
}


/* SimCtrl destructor */
SSimCtrl::~SSimCtrl()
{
	if (isinit) emu_done();
}


/* set VE boundary for emulation, return 0 iff succeed */
int SSimCtrl::set_ve_boundary(int idbase, int nve)
{
	if (isinit) return -EPERM;
	if (idbase <= 0) return -EINVAL;
	if (nve <= 0) return -EINVAL;
	this->idbase = idbase;
	this->nve = nve;
	venif.resize(nve);
	for (int i = 0; i < nve; i++) venif[i] = 1;
	return 0;
}


/* get a VEHandle by ID */
VEHandle* SSimCtrl::get_vehandle(int ctid)
{
	if (!isinit) return NULL;
	if (ctid < idbase || ctid >= idbase+nve) return NULL;
	return ve[vemap[ctid-idbase]];
}


/* set the min delay that an outgoing packet of a VE may affect other VEs, return 0 iff succeed */
int SSimCtrl::set_ve_mindelay(int ctid, ltime_t delay)
{
	if (!isinit) return -ENXIO;
	if (la_type) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	if (delay < 0) return -EINVAL;
	ve[vemap[ctid-idbase]]->mindelay = delay;
	return 0;
}


/* get the min delay of a VE */
ltime_t SSimCtrl::get_ve_mindelay(int ctid)
{
	if (!isinit) return -ENXIO;
	if (la_type) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	return ve[vemap[ctid-idbase]]->mindelay;
}


/* set the number of interfaces of a VE, return 0 iff succeed */
int SSimCtrl::set_ve_nif(int ctid, int nif)
{
	if (isinit) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve || nif < 0) return -ERANGE;
	venif[ctid-idbase] = nif;
	return 0;
}


/* get the number of interfaces of a VE */
int SSimCtrl::get_ve_nif(int ctid)
{
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	return venif[ctid-idbase];
}


/* set the IP address of an interface, return 0 iff succeed */
int SSimCtrl::set_ve_ifip(int ctid, int ifid, const char* ip)
{
	if (isinit) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;
	if (ifid < 0 || ifid >= venif[ctid-idbase]) return -ERANGE;

	if (strlen(ip) > 64) return -E2BIG;
	char cmd[100];

	sprintf(cmd, "ifconfig veth%d.%d %s", ctid, ifid, ip);
	if (system(cmd)) return -ENOEXEC;
	sprintf(cmd, "vzctl exec %d ifconfig eth%d %s", ctid, ifid, ip);
	if (system(cmd)) return -ENOEXEC;

	return 0;
}


/* execute a command on a VE, return 0 iff succeed */
int SSimCtrl::exec_ve_command(int ctid, const char *cmd)
{
	if (!isinit) return -EPERM;
	if (ctid < idbase || ctid >= idbase+nve) return -ERANGE;

	if (strlen(cmd) > 160) return -E2BIG;
	char ccmd[200];
	sprintf(ccmd, "vzctl exec %d \"%s &\"", ctid, cmd);
	if (system(cmd)) return -ENOEXEC;
	return 0;
}


/* emulation initialization: call once before emulation starts, return 0 iff succeed */
int SSimCtrl::emu_init(int la_type)
{
	envid_t idlist[EMU_MAX_VE];
	struct timeval tv;
	int n;

	if (isinit) return -EEXIST;
	if (nve <= 0) return -EINVAL;
	tv.tv_sec = tv.tv_usec = 0;

	/* validate vtime VEs */
	for (int i = 0; i < nve; i++)
		if (vtime_setenable(idbase+i, 0)) return -EINTR;
	if (vtime_getvtimeve(&n, idlist)) return -EINTR;
	if (n != 0) return -ESRCH;

	/* enable vtime switches */
	for (int i = 0; i < nve; i++) {
		if (vtime_setenable(idbase+i, 1)) return -EINTR;
		if (vtime_setclock(idbase+i, &tv)) return -EINTR;
	}

	/* init VE mapping */
	isinit = true;
	this->la_type = la_type;
	vemap.resize(nve);
	ve.resize(nve);
	vtime_getvtimeve(&n, idlist);
	for (int i = 0; i < nve; i++) vemap[idlist[i]-idbase] = i;

	/* init VEHandle */
	for (int i = 0; i < nve; i++) {
		VEHandle *p = new VEHandle(idbase + i, la_type, venif[i]);
		if (!p) return -EINTR;
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
			if (!p->pcap[j]) return -ENODEV;
			if (pcap_setdirection((pcap_t*)p->pcap[j], PCAP_D_IN)) return -EIO;
			if (pcap_setnonblock((pcap_t*)p->pcap[j], 1, errbuf)) return -EIO;
		}
	}

#ifdef LOGGING
	pf = fopen("simctrl.log", "w");
	if (!pf) return -EIO;
#endif

#ifdef TIME_STAT
	firsttime = true;
#endif

	return 0;
}


/* emulation clean up: call once after emulation ends, return 0 iff succeed */
int SSimCtrl::emu_done()
{
	if (!isinit) return -ENXIO;

	/* disable virtual time switches */
	for (int i = 0; i < nve; i++)
		vtime_setenable(idbase+i, 0);

	/* delete VEHandle */
	for (int i = 0; i < nve; i++) if (ve[i]) delete ve[i];
	vemap.clear();
	ve.clear();

#ifdef LOGGING
	if (pf) fclose(pf);
#endif

	isinit = false;
	return 0;
}


/* emulation reset */
int SSimCtrl::emu_reset(bool close)
{
	for (int i = 0; i < nve; i++)
		vtime_setenable(idbase+i, 0);
	isinit = false;
	return 0;
}


/* for pcap_dispatch */
void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{
	VEHandle *pve = (VEHandle*)args;
	EmuPacket *ppkt = new EmuPacket(header->len);
	ppkt->ifid = pve->nowif;
	memcpy(ppkt->data, packet, header->len);
	ppkt->timestamp = (header->ts.tv_sec%1000)*1000000 + header->ts.tv_usec;
	if (ppkt->timestamp > pve->get_clock()) ppkt->timestamp = pve->get_clock();
	if (ppkt->timestamp < pve->get_lastc()) ppkt->timestamp = pve->get_lastc();
	pve->sendq.push_back(ppkt);
}


#ifdef TIME_STAT
/* update timestat value using exponential moving average */
inline void timestat_update(double &old, double add)
{
	old = old * 0.99 + add * 0.01;
}
#endif


/* emulation cycle: advance all VEs to the barrier, return 0 iff succeed */
int SSimCtrl::emu_cycle(ltime_t barrier)
{
	if (!isinit) return -ENXIO;
	if (barrier <= this->barrier) return -EINVAL;


#ifdef TIME_STAT
	timestat_counter = (timestat_counter + 1) % timestat_interval;
	if (debug) printf("########## start of cycle ##########\n");

	if (timestat_interval == 1 || timestat_counter == 1) {
		if (!firsttime) {
			gettimeofday(&tv2, NULL);
			timestat_update(nstime, VTIME_CLOCKDIFFU(tv2, tv1));
		}
		else {
			gettimeofday(&lastprint, NULL);
			lastvtime = 0;
			firsttime = false;
		}
	}
	if (!timestat_counter) gettimeofday(&tv1, NULL);

#endif

	vector<bool> done(nve), sent(nve);
	for (int i = 0; i < nve; i++) {
		done[i] = false; sent[i] = false;
		ve[i]->idle= false; ve[i]->idlecount = 0;
	}
	bool alldone = false, onedone = false;
	ltime_t maxadv = barrier;

	bool newbarrier = true;
	struct timeval tvbarrier;
	tvbarrier.tv_sec = barrier / 1000000;
	tvbarrier.tv_usec = barrier % 1000000;

	/* step 1: lookahead pre_cycle for all VEs */
	if (la_type) {
		for (int i = 0; i < nve; i++) if (ve[i]->la)
			((Lookahead*)ve[i]->la)->pre_cycle(barrier);
#ifdef TIME_STAT
		if (!timestat_counter) {
			gettimeofday(&tv2, NULL);
			timestat_update(latime1, VTIME_CLOCKDIFFU(tv2, tv1));
			tv1 = tv2;
		}
#endif
	}

	/* while not all VE is at the barrier */
	while (!alldone)
	{
		struct timeval tv;
		int rllist[EMU_MAX_VE];
		vtime_vestatus_t stlist[EMU_MAX_VE];
		struct vtime_trap trlist[VTIME_SZ_TRAPLIST];
		int rtlist[VTIME_SZ_TRAPLIST];
		int count;

		if (debug) printf("----- start of round, barrier=%ld-----\n", barrier);

		/* step 2: set the new barrier to all VEs */
		if (newbarrier) {
			if (debug) printf("set barrier to VEs: %ld.%06ld\n", tvbarrier.tv_sec, tvbarrier.tv_usec);
			for (int i = 0; i < nve; i++)
				if (vtime_setbarrier(ve[i]->id, &tvbarrier)) return -EINTR;
			newbarrier = false;
		}

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
					pcap_inject((pcap_t*)ve[i]->pcap[ppkt->ifid],
						    ppkt->data, ppkt->len);
				else
					fprintf(stderr, "simctrl warning: "
						"ifid=%d, nif=%d, packet dropped\n",
						ppkt->ifid, ve[i]->nif);
#ifdef LOGGING
				fprintf(pf, "vcpush\tve%d\t%.6lf\t%d\n",
					ve[i]->id, ve[i]->clock/1000000.0, ppkt->len);
#endif
				delete ppkt;
			}
			/* push due timers */
			priority_queue<EmuTimer> *pq2 = &(ve[i]->timerq);
			while (!pq2->empty() && pq2->top().due <= ve[i]->clock) {
				EmuTimer timer = pq2->top();
				pq2->pop();
				ve[i]->idle = false;
				if (timer.pid) vtime_firetimer(timer.pid);
				if (timer.repeat) {
					timer.due = ve[i]->clock + timer.repeat;
					pq2->push(timer);
				}
			}
		}
		
#ifdef TIME_STAT
		if (!timestat_counter) {
			gettimeofday(&tv2, NULL);
			timestat_update(vctime1, VTIME_CLOCKDIFFU(tv2, tv1));
			tv1 = tv2;
		}
#endif

		/* step 4: release selected VEs */
		maxadv = barrier;
		for (int i = 0; !la_type && i < nve; i++) if (!done[i]) {
			ltime_t temp = ve[i]->clock +
				       max(ve[i]->mindelay, (ltime_t)VTIME_VTIMESLICE);
			if (temp < maxadv) maxadv = temp;
		}
		if (debug) printf("maxadv=%ld\n", maxadv);
		for (int i = 0; i < nve; i++) {
			rllist[i] = (!done[i])
				    && (!ve[i]->idle || ve[i]->idlecount > MAX_IDLE_COUNT)
				    && (ve[i]->clock < maxadv - min(ve[i]->offset, 0L));
			if (debug) printf("VE%d clock=%ld rllist=%d done=%d "
					"idle=%d idlecount=%d offset=%ld\n",
					ve[i]->id, ve[i]->clock, (int)rllist[i],
					(int)done[i], (int)ve[i]->idle,
					ve[i]->idlecount, ve[i]->offset);
			if (rllist[i]) ve[i]->idlecount = 0;
		}
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
				if (count > 10 && stlist[i] == VTIME_READY)
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

#ifdef TIME_STAT
		if (!timestat_counter) {
			gettimeofday(&tv2, NULL);
			timestat_update(vetime, VTIME_CLOCKDIFFU(tv2, tv1));
			tv1 = tv2;
		}
#endif

		/* step 6: collect sent packets */
		for (int i = 0; i < nve; i++) if (rllist[i])
		{
			for (int j = 0; j < ve[i]->nif; j++)
			{
				ve[i]->nowif = j;
				while (pcap_dispatch((pcap_t*)ve[i]->pcap[j],
			 			-1, got_packet, (u_char*)ve[i]) > 0)
					stlist[i] = VTIME_SUSPENDED;
			}
			if (!la_type && !onedone && !sent[i] && !ve[i]->sendq.empty())
			{
				sent[i] = true;
				ltime_t temp = ve[i]->sendq.front()->timestamp;
				if (debug) printf("VE%d sent its first packet at time %ld\n",
						ve[i]->id, temp);
				temp += max(ve[i]->mindelay, (ltime_t)VTIME_VTIMESLICE);
				if (temp < barrier) {
					if (debug) printf("barrier shrinked to %ld\n", temp);
					barrier = temp;
					tvbarrier.tv_sec = barrier / 1000000;
					tvbarrier.tv_usec = barrier % 1000000;
					newbarrier = true;
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
			if (debug) printf("VE%d clock=%ld lastc=%ld\n",
					ve[i]->id, ve[i]->clock, ve[i]->lastclock);
			if (stlist[i] != VTIME_IDLE) 
				ve[i]->avgrun = (9 * (ve[i]->avgrun) +
						(ve[i]->clock - ve[i]->lastclock)) / 10;
		}

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
				ltime_t next = maxadv, ts;
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
					if (debug) printf("VE%d fast advance to %ld\n", ve[i]->id, next);
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
				if (debug) printf("VE%d done\n", ve[i]->id);
				if (vtime_setclock(ve[i]->id, &tvbarrier)) return -EINTR;
				if (stlist[i] == VTIME_IDLE) ve[i]->offset = 0;
					else ve[i]->offset += ve[i]->clock - barrier;
				ve[i]->lastclock = ve[i]->clock = barrier;
				done[i] = true;
				onedone = true;
			}
			else
				alldone = false;
		}

		if (debug) {
			char bp[1000];
			printf("one round done, press enter to continue\n");
			if (!debug_nopause) fgets(bp, 1000, stdin);
			if (strcmp(bp, "exit") == 0) debug_nopause = true;
		}
	} /* end of while (!alldone), if not all done, go back to step 3 */

#ifdef TIME_STAT
	if (!timestat_counter) {
		gettimeofday(&tv2, NULL);
		timestat_update(vctime2, VTIME_CLOCKDIFFU(tv2, tv1));
		tv1 = tv2;
	}
#endif

	/* step 10: no longer exist */
	if (la_type) {
		for (int i = 0; i < nve; i++) if (ve[i]->la)
			((Lookahead*)ve[i]->la)->post_cycle(barrier);
#ifdef TIME_STAT
		if (!timestat_counter) {
			gettimeofday(&tv2, NULL);
			timestat_update(latime2, VTIME_CLOCKDIFFU(tv2, tv1));
			tv1 = tv2;
		}
#endif
	}

#ifdef TIME_STAT
	lastbarrier = this->barrier;
	this->barrier = barrier;
	if (!timestat_counter) timestat_update(avgesw, barrier - lastbarrier);

	if (!timestat_counter)
	{
		gettimeofday(&tv2, NULL);
		int elapse = VTIME_CLOCKDIFFU(tv2, lastprint);
		if (elapse > 1000000)
		{
			double speed = (double)(barrier - lastvtime) / elapse;
			lastprint = tv2;
			lastvtime = barrier;
			double vctime = vctime1 + vctime2;
			double latime = latime1 + latime2;
			double alltime = vctime + vetime + nstime + (la_type ? latime : 0);
			if (la_type)
				printf("vtime=%d.%03d speed=%.3f ns=%.0lf/%.0f%% vc=%.0lf/%.0f%% "
					"ve=%.0lf/%.0f%% la=%.0lf/%.0f%% avgesw=%.0f\n",
					(int)(barrier/1000000), (int)(barrier%1000000/1000), speed,
					nstime, nstime*100/alltime,
					vctime, vctime*100/alltime,
					vetime, vetime*100/alltime,
					latime, latime*100/alltime,
					avgesw);
			else
				printf("vtime=%d.%03d speed=%.3f ns=%.0lf/%.0f%% vc=%.0lf/%.0f%% "
					"ve=%.0lf/%.0f%% avgesw=%.0f\n",
					(int)(barrier/1000000), (int)(barrier%1000000/1000), speed,
					nstime, nstime*100/alltime,
					vctime, vctime*100/alltime,
					vetime, vetime*100/alltime,
					avgesw);
		}
	}
#endif
	if (debug) printf("########## end of cycle ##########\n");

	return 0;
}

