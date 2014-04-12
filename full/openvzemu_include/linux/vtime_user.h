/*
 * vtime_user.h : virtual time system for OpenVZ-based network emulation
 *
 * authors : Yuhao Zheng <zheng7@illinois.edu>
 */

#ifndef _LINUX_VTIME_USER_H
#define _LINUX_VTIME_USER_H

#ifndef __ENVID_T_DEFINED__
typedef unsigned envid_t;
#define __ENVID_T_DEFINED__
#endif

#define VTIME_TIMESLICE 100	/* in usec, VE scheduling granularity */
#define VTIME_IDLETHRESHOLD 20	/* in usec, VE idle advnace test threshold */
#define VTIME_CPUTIMESCALE >>1 	/* scale factor of CPU time added to virtual clock */
#define VTIME_SZ_TRAPLIST 100	/* length of the trap list for each VE */
#define VTIME_SZ_TRAPDATA 4	/* length of data field of each trap */

#define VTIME_VTIMESLICE (VTIME_TIMESLICE VTIME_CPUTIMESCALE)	/* for simctrl use */
#define VTIME_VIDLETHRESHOLD (VTIME_IDLETHRESHOLD VTIME_CPUTIMESCALE)	/* for simctrl use */

/* =============================================================================
 *  data structures used by both kernel space and user space
 * =============================================================================
 */ 

/* enum of VE running status */
typedef enum vtime_vestatus_t {
	VTIME_UNSTABLE = 0,		/* for atomic test only, should not use */
	VTIME_RELEASED,			/* can be scheduled by the scheduler */
	VTIME_SUSPENDED,		/* cannot run */
	VTIME_READY,			/* have been scheduled by the scheduler */
	VTIME_RUNNING,			/* running */
	VTIME_IDLE,			/* no runnable tasks */
	VTIME_YIELD,			/* VE voluntarily yields its timeslice */
} vtime_vestatus_t;

/* enum of trap types */
typedef enum vtime_traptype_t {
	VTIME_TRAPTIMER = 1,
	VTIME_TRAPSLEEP,
} vtime_traptype_t;

/* enum of trap status */
typedef enum vtime_trapstatus_t {
	VTIME_TRAPNEW,
	VTIME_TRAPPENDING,
	VTIME_TRAPDONE,
} vtime_trapstatus_t;

/* struct for syscall trap */
struct vtime_trap {
	vtime_traptype_t type;		/* trap type */
	struct timeval time;		/* virtual time of the trap */
	int pid;			/* pid instead of task pointer */
	long data[VTIME_SZ_TRAPDATA];	/* data field */
#ifdef __KERNEL__
	atomic_t released;		/* status of this trap */
#else
	int released;			/* for user space */
#endif
};


/* timeval struct operations */

#define VTIME_CLOCKCARRY(clk)				\
	while ((clk).tv_usec >= 1000000) {		\
		(clk).tv_usec -= 1000000;		\
		(clk).tv_sec++;				\
	}						\
	while ((clk).tv_usec < 0) {			\
		(clk).tv_usec += 1000000;		\
		(clk).tv_sec--;				\
	}

#define VTIME_CLOCKADD(src, diff)			\
	(src).tv_sec += (diff).tv_sec;			\
	(src).tv_usec += (diff).tv_usec;		\
	VTIME_CLOCKCARRY(src);

#define VTIME_CLOCKADDU(src, usec)			\
	(src).tv_usec += usec;				\
	VTIME_CLOCKCARRY(src);

#define VTIME_CLOCKSUB(src, diff)			\
	(src).tv_sec -= (diff).tv_sec;			\
	(src).tv_usec -= (diff).tv_usec;		\
	VTIME_CLOCKCARRY(src);

#define VTIME_CLOCKSUBU(src, usec)			\
	(src).tv_usec -= usec;				\
	VTIME_CLOCKCARRY(src);

#define VTIME_CLOCKDIFFU(clk1, clk2)			\
	((clk1).tv_usec - (clk2).tv_usec +		\
	 ((clk1).tv_sec - (clk2).tv_sec) * 1000000)

#define VTIME_CLOCKCMP(clk1, clk2)			\
	((clk1).tv_sec > (clk2).tv_sec ? 1 :		\
	 (clk1).tv_sec < (clk2).tv_sec ? -1 :		\
	 (clk1).tv_usec > (clk2).tv_usec ? 1 :		\
	 (clk1).tv_usec < (clk2).tv_usec ? -1 :		\
	 0)

#endif /* _LINUX_VTIME_USER_H */

