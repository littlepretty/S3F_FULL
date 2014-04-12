/** \file vtime_syscall.cc
 * \brief source file for the virtual time system calls
 *
 * authors : Yuhao Zheng
 */

#include <simctrl/vtime_syscall.h>
#include <sys/syscall.h>
#include <unistd.h>


long vtime_getenable(envid_t ctid, int *enable)
{
	return syscall(__NR_vtime_getenable, ctid, enable);
}


long vtime_setenable(envid_t ctid, int enable)
{
	return syscall(__NR_vtime_setenable, ctid, enable);
}


long vtime_getvtimeve(int *size, envid_t *list)
{
	return syscall(__NR_vtime_getvtimeve, size, list);
}


long vtime_getclock(envid_t ctid, struct timeval *tv)
{
	return syscall(__NR_vtime_getclock, ctid, tv);
}


long vtime_setclock(envid_t ctid, struct timeval *tv)
{
	return syscall(__NR_vtime_setclock, ctid, tv);
}


long vtime_setbarrier(envid_t ctid, struct timeval *tv)
{
	return syscall(__NR_vtime_setbarrier, ctid, tv);
}


long vtime_getvestatus(int *size, vtime_vestatus_t *list)
{
	return syscall(__NR_vtime_getvestatus, size, list);
}


long vtime_releaseve(int size, int *list)
{
	return syscall(__NR_vtime_releaseve, size, list);
}


long vtime_gettraplist(envid_t ctid, int *size, struct vtime_trap *list)
{
	return syscall(__NR_vtime_gettraplist, ctid, size, list);
}


long vtime_releasetrap(envid_t ctid, int size, int *flag)
{
	return syscall(__NR_vtime_releasetrap, ctid, size, flag);
}


long vtime_firetimer(pid_t pid)
{
	return syscall(__NR_vtime_firetimer, pid);
}

