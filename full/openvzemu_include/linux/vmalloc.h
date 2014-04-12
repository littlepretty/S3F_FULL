#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <asm/page.h>		/* pgprot_t */

struct vm_area_struct;

/* bits in vm_struct->flags */
#define VM_IOREMAP	0x00000001	/* ioremap() and friends */
#define VM_ALLOC	0x00000002	/* vmalloc() */
#define VM_MAP		0x00000004	/* vmap()ed pages */
#define VM_USERMAP	0x00000008	/* suitable for remap_vmalloc_range */
#define VM_VPAGES	0x00000010	/* buffer for pages was vmalloc'ed */
/* bits [20..32] reserved for arch specific ioremap internals */

/*
 * Maximum alignment for ioremap() regions.
 * Can be overriden by arch-specific value.
 */
#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/* 128 pages */
#endif

/* align size to 2^n page boundary */
#define POWER2_PAGE_ALIGN(size) \
	((typeof(size))(1UL << (PAGE_SHIFT + get_order(size))))

struct vm_struct {
	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
	unsigned int		nr_pages;
	unsigned long		phys_addr;
	struct vm_struct	*next;
};

/*
 *	Highlevel APIs for driver use
 */
extern void *vmalloc(unsigned long size);
extern void *ub_vmalloc(unsigned long size);
extern void *vmalloc_user(unsigned long size);
extern void *vmalloc_node(unsigned long size, int node);
extern void *ub_vmalloc_node(unsigned long size, int node);
extern void *vmalloc_exec(unsigned long size);
extern void *vmalloc_32(unsigned long size);
extern void *vmalloc_32_user(unsigned long size);
extern void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot);
extern void *vmalloc_best(unsigned long size);
extern void *ub_vmalloc_best(unsigned long size);
extern void *__vmalloc_area(struct vm_struct *area, gfp_t gfp_mask,
				pgprot_t prot);
extern void *__vmalloc_node(unsigned long size, gfp_t gfp_mask,
				pgprot_t prot, int node);
extern void vfree(void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
extern void vunmap(void *addr);

extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);
 
/*
 *	Lowlevel-APIs (not for driver use!)
 */

static inline size_t get_vm_area_size(const struct vm_struct *area) 
{ 
	/* return actual size without guard page */ 
	return area->size - PAGE_SIZE; 
}

extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);
extern struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
					unsigned long start, unsigned long end);
extern struct vm_struct * get_vm_area_best(unsigned long size,
					   unsigned long flags);
extern void vprintstat(void);
extern struct vm_struct *get_vm_area_node(unsigned long size,
					unsigned long flags, int node);
extern struct vm_struct *remove_vm_area(void *addr);
extern struct vm_struct *__remove_vm_area(void *addr);
extern int map_vm_area(struct vm_struct *area, pgprot_t prot,
			struct page ***pages);
extern void unmap_vm_area(struct vm_struct *area);

/*
 *	Internals.  Dont't use..
 */
extern rwlock_t vmlist_lock;
extern struct vm_struct *vmlist;

#endif /* _LINUX_VMALLOC_H */
