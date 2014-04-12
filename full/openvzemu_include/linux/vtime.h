/*
 * vtime.h : virtual time system for OpenVZ-based network emulation
 *
 * authors : Yuhao Zheng <zheng7@illinois.edu>
 */

#ifndef _LINUX_VTIME_H
#define _LINUX_VTIME_H

#include <linux/ve.h>
#include <linux/spinlock_types.h>
#include <linux/vtime_user.h>

/* =============================================================================
 *  data structures used by kernel space only
 * =============================================================================
 */ 

/* per-VE vtime handler */
struct ve_vtime {
	atomic_t enabled;		/* vtime enabled? */
	atomic_t status;		/* status of VE */
	struct fairsched_node *fsched;	/* pointer to fairsched node */

	rwlock_t lock_clock;		/* lock for the clock */
	struct timeval clock;		/* virtual clock */
	struct timeval runclock;	/* wall clock time reference */
	struct timeval barrier;		/* barrier for the virtual clock */

	rwlock_t lock_trap;		/* lock for the trap list */
	int nr_trap;			/* size of the trap list */
	struct vtime_trap* trap[VTIME_SZ_TRAPLIST]; /* trap list */
};


/* =============================================================================
 *  public functions
 * =============================================================================
 */ 

/* init/fini vtime struct, called on VE create/destroy */
int vtime_init(struct ve_struct* ve);
void vtime_fini(struct ve_struct* ve);

/* for virtual clock advance, called by scheduler */
void vtime_vestartrun(struct ve_struct *ve);
void vtime_vestoprun(struct ve_struct *ve);

/* VE release test, called by scheduler */
int vtime_releasetest(struct ve_struct *ve);

/* special trap routines */
int vtime_gettimeofday(struct timeval *tv);
int vtime_setitimer(struct itimerval *itv);
int vtime_trapevent(vtime_traptype_t type);
int vtime_nanosleep(struct timespec *ts);

/* trap VE routine and suspend */ 
int vtime_suspend(vtime_traptype_t type, int nonblock);

/* did VE yield its current timeslice? */
int vtime_isveyield(struct ve_struct *ve);

/* =============================================================================
 *  system calls
 * =============================================================================
 */ 

/* get/set whether vtime is enabled for a given VE */
asmlinkage long sys_vtime_getenable(envid_t ctid, int *enable);
asmlinkage long sys_vtime_setenable(envid_t ctid, int enable);

/* get the list of all vtime enabled VEs */
asmlinkage long sys_vtime_getvtimeve(int *size, envid_t *list);

/* get/set virtual clock of a given VE */ 
asmlinkage long sys_vtime_getclock(envid_t ctid, struct timeval *tv);
asmlinkage long sys_vtime_setclock(envid_t ctid, struct timeval *tv);

/* set a vtime barrier for a VE */
asmlinkage long sys_vtime_setbarrier(envid_t ctid, struct timeval *tv);

/* get the running status of all vtime enabled VEs */
asmlinkage long sys_vtime_getvestatus(int *size, vtime_vestatus_t *list);

/* release some VE enabled VEs */
asmlinkage long sys_vtime_releaseve(int size, int *list);

/* get the trap list of a given VE */
asmlinkage long sys_vtime_gettraplist(envid_t ctid,
		int *size, struct vtime_trap *list);

/* release some traps of a given VE */
asmlinkage long sys_vtime_releasetrap(envid_t ctid, int size, int *flag);

/* fire timers, data structure to be decided */
asmlinkage long sys_vtime_firetimer(pid_t pid);

#endif /* _LINUX_VTIME_H */
