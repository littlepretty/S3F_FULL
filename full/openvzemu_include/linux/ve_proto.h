/*
 *  include/linux/ve_proto.h
 *
 *  Copyright (C) 2005  SWsoft
 *  All rights reserved.
 *  
 *  Licensing governed by "linux/COPYING.SWsoft" file.
 *
 */

#ifndef __VE_H__
#define __VE_H__

#ifdef CONFIG_VE

struct ve_struct;

struct seq_file;

typedef void (*ve_seq_print_t)(struct seq_file *, struct ve_struct *);

void vzmon_register_veaddr_print_cb(ve_seq_print_t);
void vzmon_unregister_veaddr_print_cb(ve_seq_print_t);

#ifdef CONFIG_INET
void ip_fragment_cleanup(struct ve_struct *envid);
void tcp_v4_kill_ve_sockets(struct ve_struct *envid);
#ifdef CONFIG_VE_NETDEV
int venet_init(void);
#endif
#else
static inline void ip_fragment_cleanup(struct ve_struct *ve) { ; }
#endif

extern struct list_head ve_list_head;
#define for_each_ve(ve)	list_for_each_entry((ve), &ve_list_head, ve_list)
extern rwlock_t ve_list_lock;
extern struct ve_struct *get_ve_by_id(envid_t);
extern struct ve_struct *__find_ve_by_id(envid_t);

struct env_create_param3;
extern int real_env_create(envid_t veid, unsigned flags, u32 class_id,
			   struct env_create_param3 *data, int datalen);
extern void ve_move_task(struct task_struct *, struct ve_struct *);

int set_device_perms_ve(envid_t veid, unsigned type, dev_t dev, unsigned mask);
int get_device_perms_ve(int dev_type, dev_t dev, int access_mode);
void clean_device_perms_ve(envid_t veid);
extern struct file_operations proc_devperms_ops;

enum {
	VE_SS_CHAIN,
	VE_INIT_EXIT_CHAIN,

	VE_MAX_CHAINS
};

struct in6_addr;
struct request_sock;
struct sock;

struct ve_ipv6_ops {
	int (*snmp_proc_init)(struct ve_struct *);
	void (*snmp_proc_fini)(struct ve_struct *);

	int (*addrconf_sysctl_init)(struct ve_struct *);
	void (*addrconf_sysctl_fini)(struct ve_struct *);
	void (*addrconf_sysctl_free)(struct ve_struct *);

	int (*ndisc_init)(struct ve_struct *);
	void (*ndisc_fini)(struct ve_struct *);

	int (*route_init)(struct ve_struct *);
	void (*route_fini)(struct ve_struct *);

	int (*ifdown)(struct net_device *, int);
	void (*frag_cleanup)(struct ve_struct *);
	int (*addr_add)(int, struct in6_addr *, int, __u32, __u32);
	int (*sock_mc_join)(struct sock *, int, struct in6_addr *);
	struct request_sock * (*reqsk_alloc)(void);
	void (*reqsk_queue)(struct sock *, struct request_sock *, const unsigned long);
	void (*make_sk_mapped)(struct sock *);
};

extern struct ve_ipv6_ops *ve_ipv6_ops;

static inline void ve_ipv6_ops_init(struct ve_ipv6_ops *ops)
{
	wmb();
	BUG_ON(ve_ipv6_ops != NULL);
	ve_ipv6_ops = ops;
}

static inline struct ve_ipv6_ops *ve_ipv6_ops_get(void)
{
	struct ve_ipv6_ops *ret;

	ret = ve_ipv6_ops;
	rmb();
	return ret;
}

typedef int ve_hook_init_fn(void *data);
typedef void ve_hook_fini_fn(void *data);

struct ve_hook
{
	ve_hook_init_fn *init;
	ve_hook_fini_fn *fini;
	struct module *owner;

	/* Functions are called in ascending priority */
	int priority;

	/* Private part */
	struct list_head list;
};

enum {
	HOOK_PRIO_DEFAULT = 0,

	HOOK_PRIO_FS = HOOK_PRIO_DEFAULT,

	HOOK_PRIO_NET_PRE,
	HOOK_PRIO_NET,
	HOOK_PRIO_NET_POST,
	HOOK_PRIO_NET_ACCT = 100,
	HOOK_PRIO_NET_ACCT_V6,

	HOOK_PRIO_AFTERALL = INT_MAX
};

extern int ve_hook_iterate_init(int chain, void *data);
extern void ve_hook_iterate_fini(int chain, void *data);

extern void ve_hook_register(int chain, struct ve_hook *vh);
extern void ve_hook_unregister(struct ve_hook *vh);
#else /* CONFIG_VE */
#define ve_hook_register(ch, vh)	do { } while (0)
#define ve_hook_unregister(ve)		do { } while (0)

#define get_device_perms_ve(t, d, a)	(0)
#endif /* CONFIG_VE */
#endif
