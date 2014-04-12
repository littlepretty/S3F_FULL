/** \file vtime_syscall.h
 * \brief header file for the virtual time system calls
 *
 * authors : Yuhao Zheng
 */

#ifndef __VTIME_SYSCALL_H__
#define __VTIME_SYSCALL_H__

#include <stdlib.h>
#include <linux/vtime_user.h>


long vtime_getenable(envid_t ctid, int *enable);
long vtime_setenable(envid_t ctid, int enable);
long vtime_getvtimeve(int *size, envid_t *list);
long vtime_getclock(envid_t ctid, struct timeval *tv);
long vtime_setclock(envid_t ctid, struct timeval *tv);
long vtime_setbarrier(envid_t ctid, struct timeval *tv);
long vtime_getvestatus(int *size, vtime_vestatus_t *list);
long vtime_releaseve(int size, int *list);
long vtime_gettraplist(envid_t ctid, int *size, struct vtime_trap *list);
long vtime_releasetrap(envid_t ctid, int size, int *flag);
long vtime_firetimer(pid_t pid);

#endif /* __VTIME_SYSCALL_H__ */

