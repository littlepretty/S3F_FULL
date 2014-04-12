/*
 * linux/include/asm-i386/tsc.h
 *
 * i386 TSC related functions
 */
#ifndef _ASM_i386_TSC_H
#define _ASM_i386_TSC_H

#include <asm/processor.h>

/*
 * Standard way to access the cycle counter on i586+ CPUs.
 * Currently only used on SMP.
 *
 * If you really have a SMP machine with i486 chips or older,
 * compile for that, and this will just always return zero.
 * That's ok, it just means that the nicer scheduling heuristics
 * won't work for you.
 *
 * We only use the low 32 bits, and we'd simply better make sure
 * that we reschedule before that wraps. Scheduling at least every
 * four billion cycles just basically sounds like a good idea,
 * regardless of how fast the machine is.
 */
typedef unsigned long long cycles_t;

extern unsigned int cpu_khz;
extern unsigned int tsc_khz;

static inline cycles_t get_cycles(void)
{
	unsigned long long ret = 0;

#if defined(CONFIG_X86_GENERIC) || defined(CONFIG_X86_TSC)
	rdtscll(ret);
#elif defined(CONFIG_VE)
	/*
	 * get_cycles is used in the following calculations:
	 * - VPS idle and iowait times in kernel/shced.h
	 * - task's sleep time to be shown with SyRq-t
	 * - kstat latencies in linux/vzstat.h
	 * - sched latency via wakeup_stamp in linux/ve_task.h
	 */
#warning "some of VPS statistics won't be correct without get_cycles() (kstat_lat, ve_idle, etc)"
#endif
	return ret;
}

extern void tsc_init(void);
extern void mark_tsc_unstable(void);

#endif
