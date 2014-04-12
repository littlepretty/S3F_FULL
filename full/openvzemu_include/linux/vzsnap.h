/*
 *
 * Copyright (C) 2007 SWsoft
 * All rights reserved.
 * 
 */

#ifndef _VZSNAP_H
#define _VZSNAP_H

#define VZSNAPCTLTYPE ';'

#define VZCTL_VZSNAP_NEW_CTL	_IO(VZSNAPCTLTYPE, 1)


#define VZSNAPCTL_SET_ID	0
#define VZSNAPCTL_BIND_VZFS	1
#define VZSNAPCTL_BIND_VE	2
#define VZSNAPCTL_PREPARE_DIR	3
#define VZSNAPCTL_SCAN_FD	4
#define VZSNAPCTL_RESCAN_FD	5
#define VZSNAPCTL_SCAN_NAME	6
#define VZSNAPCTL_START		7
#define VZSNAPCTL_STOP		8
#define VZSNAPCTL_GETROOT	9

#define VZSNAPCTL_GETBMAPSIZE	10
#define VZSNAPCTL_GETIMAPSIZE	11
#define VZSNAPCTL_GETBMAPMAP	12
#define VZSNAPCTL_GETIMAPMAP	13

/* ioctl request structure for VZSNAPCTL_SCAN_NAME. "Novel idea" is to use
 * 64bit interface even on 32bit hosts. I know, I know... */

struct vzsnap_name_req
{
	__s32	dirfd;
	__s32	pad;
	__u64	ptr;
} __attribute__((aligned (8)));

/* Offsets on vzsnap "bus" */

#define VZSNAP_BMAP_PGOFF	0
#define VZSNAP_IMAP_PGOFF	0x20000000

enum
{
	IS_NONE		= 0,	/* Not scanned or not within our tree */
	IS_SCANNED	= 1,	/* Inode is ours, scan is started */
	IS_RESCAN	= 3	/* Inode is ours, needs rescan */
};

#ifdef __KERNEL__

struct vzsnap_struct
{
	atomic_t		refcnt;
	unsigned long		dead;
	unsigned long		state;
	struct list_head	list;
	int			id;

	struct vzsnap_ops	*ops;

	int			error;

	int			ve_frozen;
	struct ve_struct	*ve;

	struct page		**inode_map;
	struct page		**block_map;
	ino_t			inode_max;
	sector_t		block_max;

	struct vfsmount		*vzfs_mnt;
	struct dentry		*vzfs_root;
	unsigned long		priv_ino;
	unsigned long		cow_ino;

	struct vfsmount		*vzdq_mnt;
	struct dentry		*vzdq_root;

	struct super_block	*psb;

	spinlock_t		lock;	/* Protects bitmap operations */
	struct mutex		mutex;	/* ioctl serialization */
};

struct vzsnap_ops
{
	void (*addblock)(struct vzsnap_struct *vzs, struct inode * inode);
	void (*create)(struct vzsnap_struct *vzs, struct inode *dir, struct dentry *de);
	void (*unlink)(struct vzsnap_struct *vzs, struct inode *dir, struct dentry *de);
	void (*rename)(struct vzsnap_struct *vzs, struct inode *ndir,
		   struct dentry *nde, struct inode *odir, struct dentry *ode);
	void (*truncate)(struct vzsnap_struct *vzs, struct inode *dir, size_t len);
};

/* Should be protected with user-specific serializer */

static inline struct vzsnap_struct *vzsnap_get(struct vzsnap_struct * vzs)
{
	atomic_inc(&vzs->refcnt);
	return vzs;
}

static inline void __vzsnap_put(struct vzsnap_struct * vzs)
{
	atomic_dec(&vzs->refcnt);
}

extern int vzsnap_release_map(struct vzsnap_struct *vzs);
extern struct vzsnap_struct * vzsnap_get_map(int id, struct block_device *bdev);


#endif /* __KERNEL__ */

#endif /* _VZSNAP_H */
