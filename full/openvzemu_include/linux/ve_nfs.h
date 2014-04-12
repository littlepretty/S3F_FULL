/*
 * linux/include/ve_nfs.h
 *
 * VE context for NFS
 *
 * Copyright (C) 2007 SWsoft
 */

#ifndef __VE_NFS_H__
#define __VE_NFS_H__

#ifdef CONFIG_VE
#include <linux/ve.h>

#define NFS_CTX_FIELD(arg)  (get_exec_env()->_##arg)

#define nlmsvc_grace_period	NFS_CTX_FIELD(nlmsvc_grace_period)
#define nlmsvc_timeout		NFS_CTX_FIELD(nlmsvc_timeout)
#define nlmsvc_users		NFS_CTX_FIELD(nlmsvc_users)
#define nlmsvc_pid		NFS_CTX_FIELD(nlmsvc_pid)
#define nlmsvc_serv		NFS_CTX_FIELD(nlmsvc_serv)
#else
#define nlmsvc_grace_period	_nlmsvc_grace_period
#define nlmsvc_users		_nlmsvc_users
#define nlmsvc_pid		_nlmsvc_pid
#define nlmsvc_serv		_nlmsvc_serv
#define nlmsvc_timeout		_nlmsvc_timeout
#endif

extern void nfs_change_server_params(void *data, int flags, int timeo, int retrans);

extern int ve_nfs_sync(struct ve_struct *env, int wait);
extern int is_nfs_automount(struct vfsmount *mnt);

/* This two originaly defined in linux/sunrpc/xprt.h */
#define RPC_MAX_ABORT_TIMEOUT	INT_MAX
extern int xprt_abort_timeout;
 
#endif
