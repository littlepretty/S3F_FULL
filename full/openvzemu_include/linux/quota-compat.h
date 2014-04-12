/*
 *  include/linux/quota-compat.h
 *
 *  Copyright (C) 2008  SWsoft
 *  All rights reserved.
 *
 *  Licensing governed by "linux/COPYING.SWsoft" file.
 *
 */

#ifndef _LINUX_QUOTA_COMPAT_
#define _LINUX_QUOTA_COMPAT_

#include <linux/compat.h>

#define QC_QUOTAON  0x0100	/* enable quotas */
#define QC_QUOTAOFF 0x0200	/* disable quotas */

/* GETQUOTA, SETQUOTA and SETUSE which were at 0x0300-0x0500 has now
 * other parameteres
 */
#define QC_SYNC     0x0600	/* sync disk copy of a filesystems quotas */
#define QC_SETQLIM  0x0700	/* set limits */
/* GETSTATS at 0x0800 is now longer... */
#define QC_GETINFO  0x0900	/* get info about quotas - graces, flags... */
#define QC_SETINFO  0x0A00	/* set info about quotas */
#define QC_SETGRACE 0x0B00	/* set inode and block grace */
#define QC_SETFLAGS 0x0C00	/* set flags for quota */
#define QC_GETQUOTA 0x0D00	/* get limits and usage */
#define QC_SETQUOTA 0x0E00	/* set limits and usage */
#define QC_SETUSE   0x0F00	/* set usage */
/* 0x1000 used by old RSQUASH */
#define QC_GETSTATS 0x1100	/* get collected stats */

struct compat_v2_dqblk {
	unsigned int dqb_ihardlimit;
	unsigned int dqb_isoftlimit;
	unsigned int dqb_curinodes;
	unsigned int dqb_bhardlimit;
	unsigned int dqb_bsoftlimit;
	qsize_t dqb_curspace;
	__kernel_time_t dqb_btime;
	__kernel_time_t dqb_itime;
};

#ifdef CONFIG_COMPAT
struct compat_v2_dqblk_32 {
	unsigned int dqb_ihardlimit;
	unsigned int dqb_isoftlimit;
	unsigned int dqb_curinodes;
	unsigned int dqb_bhardlimit;
	unsigned int dqb_bsoftlimit;
	qsize_t dqb_curspace;
	compat_time_t dqb_btime;
	compat_time_t dqb_itime;
} __attribute__ ((packed));
#endif

#endif /* _LINUX_QUOTA_COMPAT_ */

