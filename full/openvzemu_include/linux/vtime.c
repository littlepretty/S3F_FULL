#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/timex.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/fairsched.h>
#include <linux/vsched.h>

#include <asm/uaccess.h>
#include <linux/vtime.h>
#include <linux/ve.h>
#include <linux/ve_proto.h>


/* get the wall clock time */
inline void vtime_getwallclock(struct timeval *tv)
{
	do_gettimeofday_real(tv);
}


/* replace the system original one */
inline void do_gettimeofday(struct timeval *tv)
{
	if (vtime_gettimeofday(tv)) return;
	do_gettimeofday_real(tv);
}
EXPORT_SYMBOL(do_gettimeofday);


/* initialize the ve_vtime struct, called on VE create 
 * returns 0 iff succeed
 */
int vtime_init(struct ve_struct *ve)
{
	struct ve_vtime *new;

	if (ve_is_super(ve)) return -EPERM;

	/* allocate memory in kernel */
	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (new == NULL) return -ENOMEM;

	/* fields initialization */
	atomic_set(&new->enabled, 0);
	atomic_set(&new->status, VTIME_RELEASED);
	rwlock_init(&new->lock_clock);
	rwlock_init(&new->lock_trap);
	new->clock.tv_sec = 0;
	new->clock.tv_usec = 0;
	new->barrier.tv_sec = -1;
	new->barrier.tv_usec = 0;
	vtime_getwallclock(&new->runclock);
	new->nr_trap = 0;
	
	ve->vtime = new;
	return 0;
}
EXPORT_SYMBOL(vtime_init);


/* clean up the ve_vtime struct, called on VE destroy */
void vtime_fini(struct ve_struct *ve)
{
	if (ve_is_super(ve)) return;

	/* free memory in kernel */
	kfree(ve->vtime);
	ve->vtime = NULL;
}
EXPORT_SYMBOL(vtime_fini);


/* VE starts running, called by scheduler */
void vtime_vestartrun(struct ve_struct *ve)
{
	struct ve_vtime *vtime;
	unsigned long flag;

	if (ve_is_super(ve)) return;
	vtime = ve->vtime;
	if (!vtime) return;

	if (atomic_read(&vtime->enabled))
		atomic_set(&vtime->status, VTIME_RUNNING);

	write_lock_irqsave(&vtime->lock_clock, flag);
	vtime_getwallclock(&vtime->runclock);
	write_unlock_irqrestore(&vtime->lock_clock, flag);
}
EXPORT_SYMBOL(vtime_vestartrun);


/* VE stops running, called by scheduler */
void vtime_vestoprun(struct ve_struct *ve)
{
	struct ve_vtime *vtime;
	unsigned long flag;
	struct timeval now;
	long usec;

	if (ve_is_super(ve)) return;
	vtime = ve->vtime;
	if (!vtime) return;

	write_lock_irqsave(&vtime->lock_clock, flag);
	vtime_getwallclock(&now);
	usec = VTIME_CLOCKDIFFU(now, vtime->runclock) VTIME_CPUTIMESCALE;
	VTIME_CLOCKADDU(vtime->clock, usec);
	write_unlock_irqrestore(&vtime->lock_clock, flag);

	if (atomic_read(&vtime->enabled)) {
		if (atomic_read(&vtime->status) == VTIME_YIELD ||
			usec < VTIME_VIDLETHRESHOLD)
			atomic_set(&vtime->status, VTIME_IDLE);
		else
			atomic_set(&vtime->status, VTIME_SUSPENDED);
	}
}
EXPORT_SYMBOL(vtime_vestoprun);


/* VE release test, called by scheduler
 * returns 1 iff VE can be released
 */
int vtime_releasetest(struct ve_struct *ve)
{
	int ret = 1;
	struct ve_vtime *vtime;

	if (ve_is_super(ve)) return 1;
	vtime = ve->vtime;
	if (!vtime) return 1;
	if (!atomic_read(&vtime->enabled)) return 1;

	/* atomic test: vtime->status == RELEASE? */
	if (atomic_sub_and_test(VTIME_RELEASED, &vtime->status)) {
		atomic_set(&vtime->status, VTIME_READY);
	}
	else {
		/* restore the original vtime->status value */
		atomic_add(VTIME_RELEASED, &vtime->status);
		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL(vtime_releasetest);


/* get a new trap struct */
struct vtime_trap* vtime_newtrap(vtime_traptype_t type, int released)
{
	struct vtime_trap *trap;
	
	trap = kmalloc(sizeof(*trap), GFP_KERNEL);
	if (!trap) return NULL;

	/* init some fields */
	atomic_set(&trap->released, released);
	trap->type = type;
	trap->pid = current->pid;
	vtime_gettimeofday(&trap->time); 
	return trap;
}


/* free a trap struct */
void vtime_freetrap(struct vtime_trap *trap)
{
	kfree(trap);
}


/* add a trap to the trap list
 * returns 0 iff succeed
 */
int vtime_addtrap(struct ve_struct *ve, struct vtime_trap *trap)
{
	unsigned long flag;
	int full = 0;

	write_lock_irqsave(&ve->vtime->lock_trap, flag);
	if (ve->vtime->nr_trap >= VTIME_SZ_TRAPLIST)
		full = -1;
	else
		ve->vtime->trap[ve->vtime->nr_trap++] = trap;
	write_unlock_irqrestore(&ve->vtime->lock_trap, flag);

	if (full && printk_ratelimit())
		printk(KERN_ALERT "vtime: trap list full\n");

	return full;
}


/* virtual time yield, will yield the current VE timeslice
 * if this is the only runnable process
 */
void vtime_yield(struct ve_struct *ve)
{
	struct timeval tv1, tv2;
	long usec;

	vtime_getwallclock(&tv1);
	yield();
	vtime_getwallclock(&tv2);

	usec = VTIME_CLOCKDIFFU(tv2, tv1);

	if (usec < VTIME_IDLETHRESHOLD) {
		atomic_set(&ve->vtime->status, VTIME_YIELD);
		yield();
	}
}


/* wait for a trap to be released */
void vtime_waittrap(struct ve_struct *ve, struct vtime_trap *trap)
{
	while (!atomic_read(&trap->released)) vtime_yield(ve);
}


/* trap gettimeofday, no suspend
 * returns 1 iff trapped
 */
int vtime_gettimeofday(struct timeval *tv)
{
	unsigned long flag;
	struct ve_struct *ve;
	long usec;
	
	ve = get_exec_env();
	if (ve_is_super(ve)) return 0;
	if (!atomic_read(&ve->vtime->enabled)) return 0;

	read_lock_irqsave(&ve->vtime->lock_clock, flag);
	vtime_getwallclock(tv);
	usec = VTIME_CLOCKDIFFU(*tv, ve->vtime->runclock) VTIME_CPUTIMESCALE;
	*tv = ve->vtime->clock;
	VTIME_CLOCKADDU(*tv, usec);
	if (ve->vtime->barrier.tv_sec > 0 &&
			VTIME_CLOCKCMP(*tv, ve->vtime->barrier) > 0)
		*tv = ve->vtime->barrier; // cannot exceed the barrier
	read_unlock_irqrestore(&ve->vtime->lock_clock, flag);

	return 1;
}
EXPORT_SYMBOL(vtime_gettimeofday);


/* trap setitimer, no suspend
 * returns 1 iff trapped
 */
int vtime_setitimer(struct itimerval *itv)
{
	struct ve_struct *ve;
	struct vtime_trap *trap;

	ve = get_exec_env();
	if (ve_is_super(ve)) return 0;
	if (!atomic_read(&ve->vtime->enabled)) return 0;

	trap = vtime_newtrap(VTIME_TRAPTIMER, 1);
	if (trap) {
		trap->data[0] = itv->it_value.tv_sec;
		trap->data[1] = itv->it_value.tv_usec;
		trap->data[2] = itv->it_interval.tv_sec;
		trap->data[3] = itv->it_interval.tv_usec;
		vtime_addtrap(ve, trap); 
	}

	return 1;
}
EXPORT_SYMBOL(vtime_setitimer);


/* record a special event into the trap list
 * returns 0 iff succeed
 */
int vtime_trapevent(vtime_traptype_t type)
{
	struct ve_struct *ve;
	struct vtime_trap *trap;

	ve = get_exec_env();
	if (ve_is_super(ve)) return 0;
	if (!atomic_read(&ve->vtime->enabled)) return 0;

	trap = vtime_newtrap(type, 1);
	if (!trap) return -1;
	return vtime_addtrap(ve, trap);
}
EXPORT_SYMBOL(vtime_trapevent);
	

/* trap nanosleep, no suspend
 * returns 1 iff trapped
 */
int vtime_nanosleep(struct timespec *ts)
{
	struct ve_struct *ve;
	struct timeval tv1, tv2;
	struct vtime_trap *trap;

	ve = get_exec_env();
	if (ve_is_super(ve)) return 0;
	if (!atomic_read(&ve->vtime->enabled)) return 0;

	tv1.tv_sec = ts->tv_sec;
	tv1.tv_usec = ts->tv_nsec / 1000;
	if (!tv1.tv_sec && !tv1.tv_usec) return 1;

	vtime_gettimeofday(&tv2);
	VTIME_CLOCKADD(tv1, tv2);

	trap = vtime_newtrap(VTIME_TRAPSLEEP, 1);
	if (trap) {
		trap->data[0] = tv1.tv_sec;
		trap->data[1] = tv1.tv_usec;
		vtime_addtrap(ve, trap); 
	}

	while (1) {
		vtime_yield(ve);
		if (!atomic_read(&ve->vtime->enabled)) break;
		vtime_gettimeofday(&tv2);
		if (VTIME_CLOCKCMP(tv2, tv1) >= 0) break;
	}

	return 1;
}
EXPORT_SYMBOL(vtime_nanosleep);


/* trap VE routine and suspend */
int vtime_suspend(vtime_traptype_t type, int nonblock)
{
	struct ve_struct *ve;
	struct vtime_trap *trap;

	ve = get_exec_env();
	if (ve_is_super(ve)) return 0;
	if (!atomic_read(&ve->vtime->enabled)) return 0;

	/* prepare a new trap struct */
	trap = vtime_newtrap(type, 0);
	if (!trap) return 1;
	trap->data[0] = nonblock;

	/* wait until this trap has been released */
	if (!vtime_addtrap(ve, trap)) vtime_waittrap(ve, trap);

	/* destroy the trap struct */
	vtime_freetrap(trap);
	return 1;
}
EXPORT_SYMBOL(vtime_suspend);


/* did VE yield its current timeslice? */
int vtime_isveyield(struct ve_struct *ve)
{
	if (ve_is_super(ve)) return 0;
	if (!ve->vtime) return 0;
	if (!atomic_read(&ve->vtime->enabled)) return 0;
	return atomic_read(&ve->vtime->status) == VTIME_YIELD;
}


/* get whether vtime is enabled for a given VE */
asmlinkage long sys_vtime_getenable(envid_t ctid, int *value)
{
	struct ve_struct *ve;
	int kvalue = -1;

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve))
		kvalue = atomic_read(&ve->vtime->enabled);
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	if (copy_to_user(value, &kvalue, sizeof(kvalue))) return -EFAULT;
	return 0;
}


/* set whether vtime is enabled for a given VE */
asmlinkage long sys_vtime_setenable(envid_t ctid, int value)
{
	struct ve_struct *ve;

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve)) {
		atomic_set(&ve->vtime->enabled, (value==1));
		atomic_set(&ve->vtime->status,
			value ? VTIME_SUSPENDED : VTIME_RELEASED);
		/* release all traps */
		if (!value) {
			int i;
			write_lock_irq(&ve->vtime->lock_trap);
			for (i = 0; i < ve->vtime->nr_trap; i++)
				if (atomic_read(&ve->vtime->trap[i]->released))
					vtime_freetrap(ve->vtime->trap[i]);
				else
					atomic_set(&ve->vtime->trap[i]->released, 1);
			ve->vtime->nr_trap = 0;
			write_unlock_irq(&ve->vtime->lock_trap);
		}
	}
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	return 0;
}


/* get the list of all vtime enabled VEs */
asmlinkage long sys_vtime_getvtimeve(int *size, envid_t *list)
{
	long ret = 0;
	int n = 0;
	struct ve_struct *ve;

	read_lock(&ve_list_lock);
	for_each_ve(ve) if (!ve_is_super(ve)) {
		if (atomic_read(&ve->vtime->enabled)) {
			if (copy_to_user(list+n++, &ve->veid, sizeof(*list)))
				ret = -EFAULT;
		}
		if (ret) break;
	}
	read_unlock(&ve_list_lock);

	if (ret) return ret;
	if (copy_to_user(size, &n, sizeof(size))) return -EFAULT;
	return 0;
}


/* get the running status of all vtime enabled VEs */
asmlinkage long sys_vtime_getvestatus(int *size, vtime_vestatus_t *list)
{
	long ret = 0;
	int n = 0;
	struct ve_struct *ve;
	vtime_vestatus_t st;

	read_lock(&ve_list_lock);
	for_each_ve(ve) if (!ve_is_super(ve)) {
		if (atomic_read(&ve->vtime->enabled)) {
			st = atomic_read(&ve->vtime->status);
			if (copy_to_user(list+n++, &st, sizeof(*list)))
				ret = -EFAULT;
		}
		if (ret) break;
	}
	read_unlock(&ve_list_lock);

	if (ret) return ret;
	if (copy_to_user(size, &n, sizeof(size))) return -EFAULT;
	return 0;
}


/* helper function: set release status of a VE */
void setrelease(struct ve_struct *ve, int release)
{
	int runtask;
	unsigned long flag;
	extern spinlock_t fairsched_lock;

	// release == 0: no change
	if (!release) return;

	// release < 0: force suspended
	if (release < 0) {
		atomic_set(&ve->vtime->status, VTIME_SUSPENDED);
		return;
	}

	// release > 0: release
	spin_lock_irqsave(&fairsched_lock, flag);
	runtask = nr_running_vsched(ve->vtime->fsched->vsched);
	spin_unlock_irqrestore(&fairsched_lock, flag);

	if (runtask > 0)
		atomic_set(&ve->vtime->status, VTIME_RELEASED);
	else
		atomic_set(&ve->vtime->status, VTIME_IDLE);
}


/* release some vtime enabled VEs */
asmlinkage long sys_vtime_releaseve(int size, int *list)
{
	long ret = 0;
	int i = 0, flag;
	struct ve_struct *ve;

	read_lock(&ve_list_lock);
	for_each_ve(ve) if (!ve_is_super(ve)) {
		if (atomic_read(&ve->vtime->enabled)) {
			if (i >= size)
				ret = -EFAULT; 
			else if (copy_from_user(&flag, list+i++, sizeof(*list)))
				ret = -EFAULT;
			else 
				setrelease(ve, flag);
		}
		if (ret) break;
	}
	read_unlock(&ve_list_lock);

	return ret;
}


/* get virtual clock of a given VE */
asmlinkage long sys_vtime_getclock(envid_t ctid, struct timeval *tv)
{
	struct ve_struct *ve;
	int ret = 0;

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve)) {
		read_lock_irq(&ve->vtime->lock_clock);
		if (copy_to_user(tv, &ve->vtime->clock, sizeof(*tv)))
			ret = -EFAULT;
		read_unlock_irq(&ve->vtime->lock_clock);
	}
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	return ret;
}


/* set virtual clock of a given VE */
asmlinkage long sys_vtime_setclock(envid_t ctid, struct timeval *tv)
{
	struct ve_struct *ve;
	long ret = 0;

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve)) {
		write_lock_irq(&ve->vtime->lock_clock);
		if (copy_from_user(&ve->vtime->clock, tv, sizeof(*tv)))
			ret = -EFAULT;
		write_unlock_irq(&ve->vtime->lock_clock);
	}
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	return ret;
}


/* set a vtime barrier for a VE */
asmlinkage long sys_vtime_setbarrier(envid_t ctid, struct timeval *tv)
{
	struct ve_struct *ve;
	long ret = 0;

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve)) {
		write_lock_irq(&ve->vtime->lock_clock);
		if (copy_from_user(&ve->vtime->barrier, tv, sizeof(*tv)))
			ret = -EFAULT;
		write_unlock_irq(&ve->vtime->lock_clock);
	}
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	return ret;
}


/* get the trap list of a given VE */
asmlinkage long sys_vtime_gettraplist(envid_t ctid, int *size, struct vtime_trap *list)
{
	long ret = 0;
	int i, n;
	struct ve_struct *ve;
	struct vtime_trap **klist;

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve)) {
		read_lock_irq(&ve->vtime->lock_trap);
		n = ve->vtime->nr_trap;
		klist = ve->vtime->trap;
		if (copy_to_user(size, &n, sizeof(*size)))
			ret = -EFAULT;
		else for (i = 0; i < n; i++)
			/* copy the trap struct to user space one by one */
			if (copy_to_user(list+i, klist[i], sizeof(*list))) {
				ret = -EFAULT;
				break;
			}
		read_unlock_irq(&ve->vtime->lock_trap);
	} 
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	return ret;
}


/* release some traps of a given VE 
 * input flag list size must match the trap list size
 * list[i]=1 iff the ith trap can be released
 */
asmlinkage long sys_vtime_releasetrap(envid_t ctid, int size, int *flag)
{
	long ret = 0;
	struct ve_struct *ve;
	int kflag[VTIME_SZ_TRAPLIST];

	read_lock(&ve_list_lock);
	ve = __find_ve_by_id(ctid);
	if (ve && !ve_is_super(ve)) {
		write_lock_irq(&ve->vtime->lock_trap);

		/* size must match */
		if (size != ve->vtime->nr_trap) 
			ret = -EINVAL;

		/* copy the flag list from user space */
		else if (copy_from_user(kflag, flag, sizeof(*kflag)*size))
			ret = -EFAULT;

		/* remove all flagged traps items from trap list */
		else { 
			struct vtime_trap **p1, **p2;
			int i;

			p1 = p2 = ve->vtime->trap;
			ve->vtime->nr_trap = 0;
			for (i = 0; i < size; i++, p2++)
				if (kflag[i]) {
					if (atomic_read(&((*p2)->released)))
						vtime_freetrap(*p2);
					else
						atomic_set(&((*p2)->released), 1);
				}
				else { 
					/* trap stays in list */
					*p1++ = *p2;
					ve->vtime->nr_trap++;
				}
		}
		write_unlock_irq(&ve->vtime->lock_trap);
	} 
	read_unlock(&ve_list_lock);

	if (!ve) return -EINVAL;
	if (ve_is_super(ve)) return -EPERM;
	return ret;
}


/* fire timers, data structure to be decided */
asmlinkage long sys_vtime_firetimer(pid_t pid)
{
	int kill_proc_info(int sig, struct siginfo *info, pid_t pid);
	struct siginfo info;

	info.si_signo = SIGALRM;
	info.si_errno = 0;
	info.si_code = SI_TIMER;
	info.si_pid = current->pid;
	info.si_uid = current->uid;

	return kill_proc_info(SIGALRM, &info, pid);
}

