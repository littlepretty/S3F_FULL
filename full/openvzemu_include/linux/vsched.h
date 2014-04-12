/*
 *  include/linux/vsched.h
 *
 *  Copyright (C) 2005  SWsoft
 *  All rights reserved.
 *  
 *  Licensing governed by "linux/COPYING.SWsoft" file.
 *
 */

#ifndef __VSCHED_H__
#define __VSCHED_H__

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/fairsched.h>
#include <linux/sched.h>

#define task_vsched(tsk)	((tsk)->vsched)
#define this_vsched()		(task_vsched(current))

/* VCPU scheduler state description */
struct vcpu_struct;
struct vcpu_scheduler {
	struct list_head idle_list;
	struct list_head active_list;
	struct list_head running_list;
#ifdef CONFIG_FAIRSCHED
	struct fairsched_node *node;
#endif
	struct list_head list;
	struct vcpu_struct *vcpu[NR_CPUS];
	int id;
	cpumask_t vcpu_online_map, vcpu_running_map;
	cpumask_t pcpu_running_map;
	int num_online_vcpus;
	atomic_t nr_unint_fixup; /* nr_uninterruptible stat is added here
				      on vcpu death */
} ____cacheline_internodealigned_in_smp;

extern struct vcpu_scheduler default_vsched, idle_vsched;

extern int vsched_create(int id, struct fairsched_node *node);
extern int vsched_destroy(struct vcpu_scheduler *vsched);
extern int vsched_taskcount(struct vcpu_scheduler *vsched);

extern int vsched_mvpr(struct task_struct *p, struct vcpu_scheduler *vsched);
extern int vsched_set_vcpus(struct vcpu_scheduler *vsched, unsigned int vcpus);

unsigned long ve_scale_khz(unsigned long khz);

#endif
