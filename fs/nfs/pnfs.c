/*
 *  linux/fs/nfs/pnfs.c
 *
 *  pNFS functions to call and manage layout drivers.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dean Hildebrand <dhildebz@eecs.umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_page.h>
#include <linux/nfs4.h>
#include <linux/pnfs_xdr.h>
#include <linux/nfs4_pnfs.h>

#include "internal.h"

#include "nfs4_fs.h"
#include "pnfs.h"

#ifdef CONFIG_PNFS
#define NFSDBG_FACILITY		NFSDBG_PNFS

#define MIN_POOL_LC		(4)

static int pnfs_initialized;

/* Locking:
 *
 * pnfs_spinlock:
 * 	protects pnfs_modules_tbl.
 */
static spinlock_t pnfs_spinlock = __SPIN_LOCK_UNLOCKED(pnfs_spinlock);

/*
 * pnfs_modules_tbl holds all pnfs modules
 */
static struct list_head	pnfs_modules_tbl;
static struct kmem_cache *pnfs_cachep;
static mempool_t *pnfs_layoutcommit_mempool;

static inline struct pnfs_layoutcommit_data *pnfs_layoutcommit_alloc(void)
{
	struct pnfs_layoutcommit_data *p =
			mempool_alloc(pnfs_layoutcommit_mempool, GFP_NOFS);
	if (p)
		memset(p, 0, sizeof(*p));

	return p;
}

static inline void pnfs_layoutcommit_free(struct pnfs_layoutcommit_data *p)
{
	mempool_free(p, pnfs_layoutcommit_mempool);
}

static void pnfs_layoutcommit_release(void *lcdata)
{
	pnfs_layoutcommit_free(lcdata);
}


/*
 * struct pnfs_module - One per pNFS device module.
 */
struct pnfs_module {
	struct pnfs_layoutdriver_type *pnfs_ld_type;
	struct list_head        pnfs_tblid;
};

/*
*  pnfs_layout_extents: Keep track of all byte ranges for
*  which we have requrested layout information.
*/
struct pnfs_layout_extents {
	struct list_head        ple_hash;    /* hash by "struct inode *" */
};

int
pnfs_initialize(void)
{
	INIT_LIST_HEAD(&pnfs_modules_tbl);

	pnfs_cachep = kmem_cache_create("pnfs_layoutcommit_data",
					sizeof(struct pnfs_layoutcommit_data),
					0, SLAB_HWCACHE_ALIGN, NULL);
	if (pnfs_cachep == NULL)
		return -ENOMEM;

	pnfs_layoutcommit_mempool = mempool_create(MIN_POOL_LC,
						   mempool_alloc_slab,
						   mempool_free_slab,
						   pnfs_cachep);
	if (pnfs_layoutcommit_mempool == NULL) {
		kmem_cache_destroy(pnfs_cachep);
		return -ENOMEM;
	}

	pnfs_initialized = 1;
	return 0;
}

void pnfs_uninitialize(void)
{
	mempool_destroy(pnfs_layoutcommit_mempool);
	kmem_cache_destroy(pnfs_cachep);
}

/* search pnfs_modules_tbl for right pnfs module */
static int
find_pnfs(u32 id, struct pnfs_module **module) {
	struct  pnfs_module *local = NULL;

	dprintk("PNFS: %s: Searching for %u\n", __func__, id);
	list_for_each_entry(local, &pnfs_modules_tbl, pnfs_tblid) {
		if (local->pnfs_ld_type->id == id) {
			*module = local;
			return(1);
		}
	}
	return 0;
}

/* Set context to indicate we require a layoutcommit
 * If we don't even have a layout, we don't need to commit it.
 */
void
pnfs_need_layoutcommit(struct nfs_inode *nfsi, struct nfs_open_context *ctx)
{
	dprintk("%s: current_layout=%p layoutcommit_ctx=%p ctx=%p\n", __func__,
		nfsi->current_layout, nfsi->layoutcommit_ctx, ctx);
	spin_lock(&pnfs_spinlock);
	if (nfsi->current_layout && !nfsi->layoutcommit_ctx) {
		nfsi->layoutcommit_ctx = get_nfs_open_context(ctx);
		nfsi->change_attr++;
		spin_unlock(&pnfs_spinlock);
		dprintk("%s: Set layoutcommit_ctx=%p\n", __func__,
			nfsi->layoutcommit_ctx);
		return;
	}
	spin_unlock(&pnfs_spinlock);
}

/* Update last_write_offset for layoutcommit.
 * TODO: We should only use commited extents, but the current nfs
 * implementation does not calculate the written range in nfs_commit_done.
 * We therefore update this field in writeback_done.
 */
void
pnfs_update_last_write(struct nfs_inode *nfsi, loff_t offset, size_t extent)
{
	loff_t end_pos, orig_offset = offset;

	if (orig_offset < nfsi->pnfs_write_begin_pos)
		nfsi->pnfs_write_begin_pos = orig_offset;
	end_pos = orig_offset + extent - 1; /* I'm being inclusive */
	if (end_pos > nfsi->pnfs_write_end_pos)
		nfsi->pnfs_write_end_pos = end_pos;
	dprintk("%s: Wrote %lu@%lu bpos %lu, epos: %lu\n",
		__func__,
		(unsigned long) extent,
		(unsigned long) offset ,
		(unsigned long) nfsi->pnfs_write_begin_pos,
		(unsigned long) nfsi->pnfs_write_end_pos);
}

/* Unitialize a mountpoint in a layout driver */
void
unmount_pnfs_layoutdriver(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);
	if (server->pnfs_curr_ld &&
	    server->pnfs_curr_ld->ld_io_ops &&
	    server->pnfs_curr_ld->ld_io_ops->uninitialize_mountpoint)
		server->pnfs_curr_ld->ld_io_ops->uninitialize_mountpoint(
			server->pnfs_mountid);
}

/*
 * Set the server pnfs module to the first registered pnfs_type.
 * Only one pNFS layout driver is supported.
 */
void
set_pnfs_layoutdriver(struct super_block *sb, struct nfs_fh *fh, u32 id)
{
	struct pnfs_module *mod;
	struct pnfs_mount_type *mt;
	struct nfs_server *server = NFS_SB(sb);

	if (id > 0 && find_pnfs(id, &mod)) {
		dprintk("%s: Setting pNFS module\n", __func__);
		server->pnfs_curr_ld = mod->pnfs_ld_type;
		mt = server->pnfs_curr_ld->ld_io_ops->initialize_mountpoint(
			sb, fh);
		if (!mt) {
			printk(KERN_ERR "%s: Error initializing mount point "
			       "for layout driver %u. ", __func__, id);
			goto out_err;
		}
		/* Layout driver succeeded in initializing mountpoint */
		server->pnfs_mountid = mt;
		/* Set the rpc_ops */
		server->nfs_client->rpc_ops = &pnfs_v41_clientops;
		return;
	}

	dprintk("%s: No pNFS module found for %u. ", __func__, id);
out_err:
	dprintk("Using NFSv4 I/O\n");
	server->pnfs_curr_ld = NULL;
	server->pnfs_mountid = NULL;
	return;
}

/* Allow I/O module to set its functions structure */
struct pnfs_client_operations*
pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *ld_type)
{
	struct pnfs_module *pnfs_mod;
	struct layoutdriver_io_operations *io_ops = ld_type->ld_io_ops;

	if (!pnfs_initialized) {
		printk(KERN_ERR "%s Registration failure."
		       "  pNFS not initialized.\n", __func__);
		return NULL;
	}

	if (!io_ops || !io_ops->alloc_layout || !io_ops->free_layout) {
		printk(KERN_ERR "%s Layout driver must provide "
		       "alloc_layout and free_layout.\n", __func__);
		return NULL;
	}

	if (!io_ops->alloc_lseg || !io_ops->free_lseg) {
		printk(KERN_ERR "%s Layout driver must provide "
		       "alloc_lseg and free_lseg.\n", __func__);
		return NULL;
	}

	pnfs_mod = kmalloc(sizeof(struct pnfs_module), GFP_KERNEL);
	if (pnfs_mod != NULL) {
		dprintk("%s Registering id:%u name:%s\n",
			__func__,
			ld_type->id,
			ld_type->name);
		pnfs_mod->pnfs_ld_type = ld_type;
		INIT_LIST_HEAD(&pnfs_mod->pnfs_tblid);

		spin_lock(&pnfs_spinlock);
		list_add(&pnfs_mod->pnfs_tblid, &pnfs_modules_tbl);
		spin_unlock(&pnfs_spinlock);
	}

	return &pnfs_ops;
}

/*  Allow I/O module to set its functions structure */
void
pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *ld_type)
{
	struct pnfs_module *pnfs_mod;

	if (find_pnfs(ld_type->id, &pnfs_mod)) {
		dprintk("%s Deregistering id:%u\n", __func__, ld_type->id);
		spin_lock(&pnfs_spinlock);
		list_del(&pnfs_mod->pnfs_tblid);
		spin_unlock(&pnfs_spinlock);
		kfree(pnfs_mod);
	}
}

/*
 * pNFS client layout cache
 */
#if defined(CONFIG_SMP)
#define BUG_ON_UNLOCKED_LO(lo) \
	BUG_ON(!spin_is_locked(&PNFS_NFS_INODE(lo)->lo_lock))
#else /* CONFIG_SMP */
#define BUG_ON_UNLOCKED_LO(lo) do {} while (0)
#endif /* CONFIG_SMP */

/*
 * get and lock nfs->current_layout
 */
static inline struct pnfs_layout_type *
get_lock_current_layout(struct nfs_inode *nfsi)
{
	struct pnfs_layout_type *lo;

	spin_lock(&nfsi->lo_lock);
	lo = nfsi->current_layout;
	if (lo)
		lo->refcount++;
	else
		spin_unlock(&nfsi->lo_lock);

	return lo;
}

/*
 * put and unlock nfs->current_layout
 */
static inline void
put_unlock_current_layout(struct nfs_inode *nfsi,
			    struct pnfs_layout_type *lo)
{
	struct nfs_client *clp;

	BUG_ON_UNLOCKED_LO(lo);
	BUG_ON(lo->refcount <= 0);

	if (--lo->refcount == 0 && list_empty(&lo->segs)) {
		struct layoutdriver_io_operations *io_ops =
			PNFS_LD_IO_OPS(lo);

		dprintk("%s: freeing layout %p\n", __func__, lo);
		io_ops->free_layout(lo);

		nfsi->current_layout = NULL;

		/* Unlist the inode.
		 * Note that nfsi->lo_lock must be released before getting
		 * cl_sem as the latter can sleep
		 */
		clp = NFS_SERVER(&nfsi->vfs_inode)->nfs_client;
		spin_unlock(&nfsi->lo_lock);
		down_write(&clp->cl_sem);
		spin_lock(&nfsi->lo_lock);
		if (!nfsi->current_layout)
			list_del_init(&nfsi->lo_inodes);
		up_write(&clp->cl_sem);
	}
	spin_unlock(&nfsi->lo_lock);
}

void
pnfs_layout_release(struct pnfs_layout_type *lo)
{
	struct nfs_inode *nfsi = NFS_I(lo->inode);

	spin_lock(&nfsi->lo_lock);
	put_unlock_current_layout(nfsi, lo);
}

static inline void
init_lseg(struct pnfs_layout_type *lo, struct pnfs_layout_segment *lseg)
{
	INIT_LIST_HEAD(&lseg->fi_list);
	kref_init(&lseg->kref);
	lseg->layout = lo;
}

static void
destroy_lseg(struct kref *kref)
{
	struct pnfs_layout_segment *lseg =
		container_of(kref, struct pnfs_layout_segment, kref);

	PNFS_LD_IO_OPS(lseg->layout)->free_lseg(lseg);
}

static inline void
put_lseg(struct pnfs_layout_segment *lseg)
{
	if (!lseg)
		return;
	kref_put(&lseg->kref, destroy_lseg);
}

static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end: NFS4_LENGTH_EOF;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	BUG_ON(!len);
	end = start + len;
	return end > start ? end - 1: NFS4_LENGTH_EOF;
}

/*
 * is l2 fully contained in l1?
 *   start1                             end1
 *   [----------------------------------)
 *           start2           end2
 *           [----------------)
 */
static inline int
lo_seg_contained(struct nfs4_pnfs_layout_segment *l1,
		 struct nfs4_pnfs_layout_segment *l2)
{
	u64 start1 = l1->offset;
	u64 end1 = end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = end_offset(start2, l2->length);

	return (start1 <= start2) && (end1 >= end2);
}

/*
 * is l1 and l2 intersecting?
 *   start1                             end1
 *   [----------------------------------)
 *                              start2           end2
 *                              [----------------)
 */
static inline int
lo_seg_intersecting(struct nfs4_pnfs_layout_segment *l1,
		    struct nfs4_pnfs_layout_segment *l2)
{
	u64 start1 = l1->offset;
	u64 end1 = end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = end_offset(start2, l2->length);

	return (end1 == NFS4_LENGTH_EOF || end1 > start2) &&
	       (end2 == NFS4_LENGTH_EOF || end2 > start1);
}

/*
* Get layout from server.
*    for now, assume that whole file layouts are requested.
*    arg->offset: 0
*    arg->length: all ones
*
*    for now, assume the LAYOUTGET operation is triggered by an I/O request.
*    the count field is the count in the I/O request, and will be used
*    as the minlength. for the file operation that piggy-backs
*    the LAYOUTGET operation with an OPEN, s
*    arg->minlength = count.
*/
static int
get_layout(struct inode *ino,
	   struct nfs_open_context *ctx,
	   struct nfs4_pnfs_layout_segment *range,
	   struct pnfs_layout_segment **lsegpp)
{
	int status;
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_pnfs_layoutget *lgp;

	dprintk("--> %s\n", __func__);

	lgp = kzalloc(sizeof(*lgp), GFP_KERNEL);
	if (lgp == NULL)
		return -ENOMEM;
	lgp->args.lseg.iomode = range->iomode;
	lgp->args.lseg.offset = range->offset;
	lgp->args.lseg.length = range->length;
	lgp->args.type = server->pnfs_curr_ld->id;
	lgp->args.minlength = lgp->args.lseg.length;
	lgp->args.maxcount = PNFS_LAYOUT_MAXSIZE;
	lgp->args.inode = ino;
	lgp->args.ctx = ctx;
	lgp->lsegpp = lsegpp;

	/* Retrieve layout information from server */
	status = NFS_PROTO(ino)->pnfs_layoutget(lgp);

	dprintk("<-- %s status %d\n", __func__, status);
	return status;
}

static inline int
free_matching_lseg(struct pnfs_layout_segment *lseg,
		   struct nfs4_pnfs_layout_segment *range)
{
	return (range->iomode == IOMODE_ANY ||
		lseg->range.iomode == range->iomode) &&
	       lo_seg_intersecting(&lseg->range, range);
}

static void
pnfs_free_layout(struct pnfs_layout_type *lo,
		 struct nfs4_pnfs_layout_segment *range)
{
	struct pnfs_layout_segment *lseg, *next;
	dprintk("%s:Begin lo %p offset %llu length %lld iomode %d\n",
		__func__, lo, range->offset, range->length, range->iomode);

	BUG_ON_UNLOCKED_LO(lo);
	list_for_each_entry_safe (lseg, next, &lo->segs, fi_list) {
		if (!free_matching_lseg(lseg, range))
			continue;
		dprintk("%s: freeing lseg %p iomode %d "
			"offset %llu length %lld\n", __func__,
			lseg, lseg->range.iomode, lseg->range.offset,
			lseg->range.length);
		list_del(&lseg->fi_list);
		put_lseg(lseg);
	}

	dprintk("%s:Return\n", __func__);
}

static int
return_layout(struct inode *ino, struct nfs4_pnfs_layout_segment *range,
	      enum pnfs_layoutrecall_type type)
{
	struct nfs4_pnfs_layoutreturn *lrp;
	struct nfs_server *server = NFS_SERVER(ino);
	int status = -ENOMEM;

	dprintk("--> %s\n", __func__);

	lrp = kzalloc(sizeof(*lrp), GFP_KERNEL);
	if (lrp == NULL)
		goto out;
	lrp->args.reclaim = 0;
	lrp->args.layout_type = server->pnfs_curr_ld->id;
	lrp->args.return_type = type;
	lrp->args.lseg = *range;
	lrp->args.inode = ino;

	status = server->nfs_client->rpc_ops->pnfs_layoutreturn(lrp);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}

int
pnfs_return_layout(struct inode *ino, struct nfs4_pnfs_layout_segment *range,
		enum pnfs_layoutrecall_type type)
{
	struct pnfs_layout_type *lo;
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs4_pnfs_layout_segment arg;
	int status;

	dprintk("--> %s\n", __func__);

	if (range)
		arg = *range;
	else {
		arg.iomode = IOMODE_ANY;
		arg.offset = 0;
		arg.length = ~0;
	}
	if (type == RECALL_FILE) {
		lo = get_lock_current_layout(nfsi);
		if (lo == NULL) {
			status = -EIO;
			goto out;
		}
		pnfs_free_layout(lo, &arg);
		spin_unlock(&nfsi->lo_lock);
	}

	status = return_layout(ino, &arg, type);
out:
	dprintk("<-- %s status: %d\n", __func__, status);
	return status;
}

/*
 * cmp two layout segments for sorting into layout cache
 */
static inline s64
cmp_layout(struct nfs4_pnfs_layout_segment *l1,
	   struct nfs4_pnfs_layout_segment *l2)
{
	s64 d;

	/* lower offset < higher offset */
	d = l1->offset - l2->offset;
	if (d)
		return d;

	/* read < read/write */
	d = (l1->iomode == IOMODE_RW) - (l2->iomode == IOMODE_RW);
	if (d)
		return d;

	/* longer length < shorter length */
	return l2->length - l1->length;
}

static void
pnfs_insert_layout(struct pnfs_layout_type *lo,
		   struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_segment *lp;
	int found = 0;

	dprintk("%s:Begin\n", __func__);

	BUG_ON_UNLOCKED_LO(lo);
	list_for_each_entry (lp, &lo->segs, fi_list) {
		if (cmp_layout(&lp->range, &lseg->range) > 0)
			continue;
		list_add_tail(&lseg->fi_list, &lp->fi_list);
		dprintk("%s: inserted lseg %p "
			"iomode %d offset %llu length %llu before "
			"lp %p iomode %d offset %llu length %llu\n",
			__func__, lseg, lseg->range.iomode,
			lseg->range.offset, lseg->range.length,
			lp, lp->range.iomode, lp->range.offset,
			lp->range.length);
		found = 1;
		break;
	}
	if (!found) {
		list_add_tail(&lseg->fi_list, &lo->segs);
		dprintk("%s: inserted lseg %p "
			"iomode %d offset %llu length %llu at tail\n",
			__func__, lseg, lseg->range.iomode,
			lseg->range.offset, lseg->range.length);
	}

	dprintk("%s:Return\n", __func__);
}

/* DH: Inject layout blob into the I/O module.  This must happen before
 *     the I/O module has its read/write methods called.
 */
static struct pnfs_layout_segment *
pnfs_inject_layout(struct pnfs_layout_type *lo,
		   struct nfs4_pnfs_layoutget_res *lgr,
		   int take_ref)
{
	struct pnfs_layout_segment *lseg;

	dprintk("%s Begin\n", __func__);
	/* FIXME - BUG - this is called while holding nfsi->lo_lock spinlock */
	lseg = PNFS_LD_IO_OPS(lo)->alloc_lseg(lo, lgr);
	if (!lseg || IS_ERR(lseg)) {
		if (!lseg)
			lseg = ERR_PTR(-ENOMEM);
		printk(KERN_ERR "%s: Could not allocate layout: error %ld\n",
		       __func__, PTR_ERR(lseg));
		return lseg;
	}

	init_lseg(lo, lseg);
	if (take_ref)
		kref_get(&lseg->kref);
	lseg->range = lgr->lseg;
	pnfs_insert_layout(lo, lseg);
	dprintk("%s Return %p\n", __func__, lseg);
	return lseg;
}

static struct pnfs_layout_type *
alloc_init_layout(struct inode *ino, struct layoutdriver_io_operations *io_ops)
{
	struct pnfs_layout_type *lo;

	lo = io_ops->alloc_layout(NFS_SERVER(ino)->pnfs_mountid, ino);
	if (!lo) {
		printk(KERN_ERR
			"%s: out of memory: io_ops->alloc_layout failed\n",
			__func__);
		return NULL;
	}

	lo->refcount = 1;
	INIT_LIST_HEAD(&lo->segs);
	lo->roc_iomode = 0;
	lo->inode = ino;
	return lo;
}

static int pnfs_wait_schedule(void *word)
{
	if (signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

/*
 * get, possibly allocate, and lock current_layout
 *
 * Note: If successful, nfsi->lo_lock is taken and the caller
 * must put and unlock current_layout by using put_unlock_current_layout()
 * when the returned layout is released.
 */
static struct pnfs_layout_type *
get_lock_alloc_layout(struct inode *ino,
		      struct layoutdriver_io_operations *io_ops)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct pnfs_layout_type *lo;
	int res;

	dprintk("%s Begin\n", __func__);

	while ((lo = get_lock_current_layout(nfsi)) == NULL) {
		/* Compete against other threads on who's doing the allocation,
		 * wait until bit is cleared if we lost this race.
		 */
		res = wait_on_bit_lock(
			&nfsi->pnfs_layout_state, NFS_INO_LAYOUT_ALLOC,
			pnfs_wait_schedule, TASK_INTERRUPTIBLE);
		if (res) {
			lo = ERR_PTR(res);
			break;
		}

		/* Was current_layout already allocated while we slept?
		 * If so, retry get_lock'ing it. Otherwise, allocate it.
		 */
		if (nfsi->current_layout)
			continue;

		lo = alloc_init_layout(ino, io_ops);
		if (lo) {
			struct nfs_client *clp = NFS_SERVER(ino)->nfs_client;

			down_write(&clp->cl_sem);
			/* must grab the layout lock */
			spin_lock(&nfsi->lo_lock);
			nfsi->current_layout = lo;
			list_add_tail(&nfsi->lo_inodes, &clp->cl_lo_inodes);
			up_write(&clp->cl_sem);
		} else
			lo = ERR_PTR(-ENOMEM);

		/* release the NFS_INO_LAYOUT_ALLOC bit and wake up waiters */
		clear_bit_unlock(NFS_INO_LAYOUT_ALLOC, &nfsi->pnfs_layout_state);
		wake_up_bit(&nfsi->pnfs_layout_state, NFS_INO_LAYOUT_ALLOC);
		break;
	}

#ifdef NFS_DEBUG
	if (!IS_ERR(lo))
		dprintk("%s Return %p\n", __func__, lo);
	else
		dprintk("%s Return error %ld\n", __func__, PTR_ERR(lo));
#endif
	return lo;
}

static inline int
has_matching_lseg(struct pnfs_layout_segment *lseg,
		  struct nfs4_pnfs_layout_segment *range)
{
	return (range->iomode == IOMODE_READ ||
		lseg->range.iomode == IOMODE_RW) &&
	       lo_seg_contained(&lseg->range, range);
}

/*
 * lookup range in layout
 */
static struct pnfs_layout_segment *
pnfs_has_layout(struct pnfs_layout_type *lo,
		struct nfs4_pnfs_layout_segment *range,
		int take_ref)
{
	struct pnfs_layout_segment *lseg, *ret = NULL;

	dprintk("%s:Begin\n", __func__);

	BUG_ON_UNLOCKED_LO(lo);
	list_for_each_entry (lseg, &lo->segs, fi_list) {
		if (!has_matching_lseg(lseg, range))
			continue;
		ret = lseg;
		if (take_ref)
			kref_get(&ret->kref);
	}

	dprintk("%s:Return %p\n", __func__, ret);
	return ret;
}

/* Update the file's layout for the given range and iomode.
 * Layout is retreived from the server if needed.
 * If lsegpp is given, the appropriate layout segment is referenced and
 * returned to the caller.
 */
int
pnfs_update_layout(struct inode *ino,
		   struct nfs_open_context *ctx,
		   size_t count,
		   loff_t pos,
		   enum pnfs_iomode iomode,
		   struct pnfs_layout_segment **lsegpp)
{
	struct nfs4_pnfs_layout_segment arg = {
		.iomode = iomode,
		.offset = pos,
		.length = count
	};
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_server *nfss = NFS_SERVER(ino);
	struct pnfs_layout_type *lo;
	struct pnfs_layout_segment *lseg = NULL;
	int result = -EIO;

	lo = get_lock_alloc_layout(ino, nfss->pnfs_curr_ld->ld_io_ops);
	if (IS_ERR(lo)) {
		dprintk("%s ERROR: can't get pnfs_layout_type\n", __func__);
		result = PTR_ERR(lo);
		goto out;
	}

	/* Check to see if the layout for the given range already exists */
	lseg = pnfs_has_layout(lo, &arg, lsegpp != NULL);
	if (lseg) {
		dprintk("%s: Using cached layout %p for %llu@%llu iomode %d)\n",
			__func__,
			nfsi->current_layout,
			arg.length,
			arg.offset,
			arg.iomode);

		result = 0;
		goto out_put;
	}

	/* if get layout already failed once goto out */
	if (nfsi->pnfs_layout_state & NFS_INO_LAYOUT_FAILED) {
		if (unlikely(nfsi->pnfs_layout_suspend &&
		    get_seconds() >= nfsi->pnfs_layout_suspend)) {
			dprintk("%s: layout_get resumed\n", __func__);
			nfsi->pnfs_layout_state &= ~NFS_INO_LAYOUT_FAILED;
			nfsi->pnfs_layout_suspend = 0;
		} else {
			result = 1;
			goto out_put;
		}
	}

	spin_unlock(&nfsi->lo_lock);

	result = get_layout(ino, ctx, &arg, lsegpp);
out:
	dprintk("%s end (err:%d) state 0x%lx lseg %p\n",
			__func__, result, nfsi->pnfs_layout_state, lseg);
	return result;
out_put:
	if (lsegpp)
		*lsegpp = lseg;
	put_unlock_current_layout(nfsi, lo);
	goto out;
}

void
pnfs_get_layout_done(struct pnfs_layout_type *lo,
		     struct nfs4_pnfs_layoutget *lgp,
		     int rpc_status)
{
	struct nfs4_pnfs_layoutget_res *res = &lgp->res;
	struct pnfs_layout_segment *lseg = NULL;
	struct nfs_inode *nfsi = NFS_I(lo->inode);

	dprintk("-->%s\n", __func__);

	spin_lock(&nfsi->lo_lock);

	BUG_ON(nfsi->current_layout != lo);

	lgp->status = rpc_status;
	if (rpc_status) {
		dprintk("%s: ERROR retrieving layout %d\n",
			__func__, rpc_status);

		switch (rpc_status) {
		case -ENOENT:   /* NFS4ERR_BADLAYOUT */
			/* transient error, don't mark with
			 * NFS_INO_LAYOUT_FAILED */
			lgp->status = 1;
			break;
		case -EAGAIN:   /* NFS4ERR_LAYOUTTRYLATER,
				 * NFS4ERR_RECALLCONFLICT, NFS4ERR_LOCKED
				 */
			nfsi->pnfs_layout_suspend = get_seconds() + 1;
			dprintk("%s: layout_get suspended until %ld\n",
				__func__, nfsi->pnfs_layout_suspend);
			break;
		case -EINVAL:   /* NFS4ERR_INVAL, NFSERR_BADIOMODE,
				 * NFS4ERR_UNKNOWN_LAYOUTTYPE
				 */
		case -ENOTSUPP:	/* NFS4ERR_LAYOUTUNAVAILABLE */
		case -ETOOSMALL:/* NFS4ERR_TOOSMALL */
		default:
			/* suspend layout get for ever for this file */
			nfsi->pnfs_layout_suspend = 0;
			dprintk("%s: no layout_get until %ld\n",
				__func__, nfsi->pnfs_layout_suspend);
			/* mark with NFS_INO_LAYOUT_FAILED */
			break;
		}
		goto get_out;
	}

	if (res->layout.len <= 0) {
		printk(KERN_ERR
		       "%s: ERROR!  Layout size is ZERO!\n", __func__);
		lgp->status =  -EIO;
		goto get_out;
	}

	/* Inject layout blob into I/O device driver */
	lseg = pnfs_inject_layout(lo, res, lgp->lsegpp != NULL);
	if (IS_ERR(lseg)) {
		lgp->status = PTR_ERR(lseg);
		lseg = NULL;
		printk(KERN_ERR "%s: ERROR!  Could not inject layout (%d)\n",
			__func__, lgp->status);
		goto get_out;
	}

	if (res->return_on_close) {
		lo->roc_iomode |= res->lseg.iomode;
		if (!lo->roc_iomode)
			lo->roc_iomode = IOMODE_ANY;
	}
	lgp->status = 0;

get_out:
	/* remember that get layout failed and don't try again */
	if (lgp->status < 0)
		nfsi->pnfs_layout_state |= NFS_INO_LAYOUT_FAILED;
	spin_unlock(&nfsi->lo_lock);

	/* res->layout.buf kalloc'ed by the xdr decoder? */
	kfree(res->layout.buf);

	dprintk("%s end (err:%d) state 0x%lx lseg %p\n",
		__func__, lgp->status, nfsi->pnfs_layout_state, lseg);
	if (lgp->lsegpp)
		*lgp->lsegpp = lseg;
	return;
}

/* Return true if a layout driver is being used for this mountpoint */
int
pnfs_enabled_sb(struct nfs_server *nfss)
{
	if (!nfss->pnfs_curr_ld)
		return 0;

	return 1;
}

size_t
pnfs_getthreshold(struct inode *inode, int iswrite)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	ssize_t threshold = 0;

	if (!pnfs_enabled_sb(nfss) ||
	    !nfss->pnfs_curr_ld->ld_policy_ops)
		goto out;

	if (iswrite && nfss->pnfs_curr_ld->ld_policy_ops->get_write_threshold) {
		threshold = nfss->pnfs_curr_ld->ld_policy_ops->get_write_threshold(nfsi->current_layout, inode);
		goto out;
	}

	if (!iswrite && nfss->pnfs_curr_ld->ld_policy_ops->get_read_threshold) {
		threshold = nfss->pnfs_curr_ld->ld_policy_ops->get_read_threshold(nfsi->current_layout, inode);
	}
out:
	return threshold;
}

/*
 * Ask the layout driver for the request size at which pNFS should be used
 * or standard NFSv4 I/O.  Writing directly to the NFSv4 server can
 * improve performance through its singularity and async behavior to
 * the underlying parallel file system.
 */
static int
below_threshold(struct inode *inode, size_t req_size, int iswrite)
{
	ssize_t threshold;

	threshold = pnfs_getthreshold(inode, iswrite);
	if ((ssize_t)req_size <= threshold)
		return 1;
	else
		return 0;
}

void
readahead_range(struct inode *inode, struct list_head *pages, loff_t *offset,
		size_t *count)
{
	struct page *first, *last;
	loff_t foff, i_size = i_size_read(inode);
	pgoff_t end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;
	size_t range;


	first = list_entry((pages)->prev, struct page, lru);
	last = list_entry((pages)->next, struct page, lru);

	foff = first->index << PAGE_CACHE_SHIFT;

	range = (last->index - first->index) * PAGE_CACHE_SIZE;
	if (last->index == end_index)
		range += ((i_size - 1) & ~PAGE_CACHE_MASK) + 1;
	else
		range += PAGE_CACHE_SIZE;
	dprintk("%s foff %lu, range %Zu\n", __func__, (unsigned long)foff,
		range);
	*offset = foff;
	*count = range;
}

void
pnfs_set_pg_test(struct inode *inode, struct nfs_pageio_descriptor *pgio)
{
	struct pnfs_layout_type *laytype;
	struct pnfs_layoutdriver_type *ld;

	pgio->pg_test = NULL;

	laytype = NFS_I(inode)->current_layout;
	ld = NFS_SERVER(inode)->pnfs_curr_ld;
	if (!pnfs_enabled_sb(NFS_SERVER(inode)) || !laytype)
		return;

	if (ld->ld_policy_ops)
		pgio->pg_test = ld->ld_policy_ops->pg_test;
}

static u32
pnfs_getboundary(struct inode *inode)
{
	u32 stripe_size = 0;
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct layoutdriver_policy_operations *policy_ops;
	struct nfs_inode *nfsi;
	struct pnfs_layout_type *lo;

	if (!nfss->pnfs_curr_ld)
		goto out;

	policy_ops = nfss->pnfs_curr_ld->ld_policy_ops;
	if (!policy_ops || !policy_ops->get_stripesize)
		goto out;

	/* The default is to not gather across stripes */
	if (policy_ops->gather_across_stripes &&
	    policy_ops->gather_across_stripes(nfss->pnfs_mountid))
		goto out;

	nfsi = NFS_I(inode);
	lo = get_lock_current_layout(nfsi);;
	if (lo) {
		stripe_size = policy_ops->get_stripesize(lo);
		put_unlock_current_layout(nfsi, lo);
	}
out:
	return stripe_size;
}

/*
 * rsize is already set by caller to MDS rsize.
 */
void
pnfs_pageio_init_read(struct nfs_pageio_descriptor *pgio,
		  struct inode *inode,
		  struct nfs_open_context *ctx,
		  struct list_head *pages,
		  size_t *rsize)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	size_t count = 0;
	loff_t loff;
	int status = 0;

	pgio->pg_threshold = 0;
	pgio->pg_iswrite = 0;
	pgio->pg_boundary = 0;
	pgio->pg_test = NULL;

	if (!pnfs_enabled_sb(nfss))
		return;

	/* Calculate the total read-ahead count */
	readahead_range(inode, pages, &loff, &count);

	if (count > 0 && !below_threshold(inode, count, 0)) {
		status = pnfs_update_layout(inode, ctx, count,
						loff, IOMODE_READ, NULL);
		dprintk("%s *rsize %Zd virt update returned %d\n",
					__func__, *rsize, status);
		if (status != 0)
			return;

		*rsize = NFS_SERVER(inode)->ds_rsize;
		pgio->pg_boundary = pnfs_getboundary(inode);
		if (pgio->pg_boundary)
			pnfs_set_pg_test(inode, pgio);
	}
}

void
pnfs_pageio_init_write(struct nfs_pageio_descriptor *pgio, struct inode *inode)
{
	pgio->pg_iswrite = 1;
	if (!pnfs_enabled_sb(NFS_SERVER(inode))) {
		pgio->pg_threshold = 0;
		pgio->pg_boundary = 0;
		pgio->pg_test = NULL;
		return;
	}
	pgio->pg_threshold = pnfs_getthreshold(inode, 1);
	pgio->pg_boundary = pnfs_getboundary(inode);
	pnfs_set_pg_test(inode, pgio);
}

/*
 * Get a layoutout for COMMIT
 */
void
pnfs_update_layout_commit(struct inode *inode,
			struct list_head *head,
			pgoff_t idx_start,
			unsigned int npages)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_page *nfs_page = nfs_list_entry(head->next);
	int status;

	dprintk("--> %s inode %p layout range: %Zd@%Lu\n", __func__, inode,
				(size_t)(npages * PAGE_SIZE),
				(loff_t)idx_start * PAGE_SIZE);

	if (!pnfs_enabled_sb(nfss))
		return;
	status = pnfs_update_layout(inode, nfs_page->wb_context,
				(size_t)npages * PAGE_SIZE,
				(loff_t)idx_start * PAGE_SIZE,
				IOMODE_RW,
				NULL);
	dprintk("%s  virt update status %d\n", __func__, status);
}

/* This is utilized in the paging system to determine if
 * it should use the NFSv4 or pNFS read path.
 * If count < 0, we do not check the I/O size.
 */
int
pnfs_use_read(struct inode *inode, ssize_t count)
{
	struct nfs_server *nfss = NFS_SERVER(inode);

	/* Use NFSv4 I/O if there is no layout driver OR
	 * count is below the threshold.
	 */
	if (!pnfs_enabled_sb(nfss) ||
	    (count >= 0 && below_threshold(inode, count, 0)))
		return 0;

	return 1; /* use pNFS I/O */
}

/* Called only from pnfs4 nfs_rpc_ops => a layout driver is loaded */
int
pnfs_use_ds_io(struct list_head *head, struct inode *inode, int io)
{
	struct nfs_page	*req;
	struct list_head *pos, *tmp;
	int count = 0;

	list_for_each_safe(pos, tmp, head) {
		req = nfs_list_entry(head->next);
		count += req->wb_bytes;
	}
	if (count >= 0 && below_threshold(inode, count, io))
		return 0;
	return 1; /* use pNFS data server I/O */
}

/* This is utilized in the paging system to determine if
 * it should use the NFSv4 or pNFS write path.
 * If count < 0, we do not check the I/O size.
 */
int
pnfs_use_write(struct inode *inode, ssize_t count)
{
	struct nfs_server *nfss = NFS_SERVER(inode);

	/* Use NFSv4 I/O if there is no layout driver OR
	 * count is below the threshold.
	 */
	if (!pnfs_enabled_sb(nfss) ||
	    (count >= 0 && below_threshold(inode, count, 1)))
		return 0;

	return 1; /* use pNFS I/O */
}

/* Return I/O buffer size for a layout driver
 * This value will determine what size reads and writes
 * will be gathered into and sent to the data servers.
 * blocksize must be a multiple of the page cache size.
 */
unsigned int
pnfs_getiosize(struct nfs_server *server)
{
	struct pnfs_mount_type *mounttype;
	struct pnfs_layoutdriver_type *ld;

	mounttype = server->pnfs_mountid;
	ld = server->pnfs_curr_ld;
	if (!pnfs_enabled_sb(server) ||
	    !mounttype ||
	    !ld->ld_policy_ops ||
	    !ld->ld_policy_ops->get_blocksize)
		return 0;

	return ld->ld_policy_ops->get_blocksize(mounttype);
}

void
pnfs_set_ds_iosize(struct nfs_server *server)
{
	unsigned dssize = pnfs_getiosize(server);

	/* Set buffer size for data servers */
	if (dssize > 0) {
		server->ds_rsize = server->ds_wsize =
			nfs_block_size(dssize, NULL);
		server->ds_rpages = server->ds_wpages =
			(server->ds_rsize + PAGE_CACHE_SIZE - 1) >>
			PAGE_CACHE_SHIFT;
	} else {
		server->ds_wsize = server->wsize;
		server->ds_rsize = server->rsize;
		server->ds_rpages = server->rpages;
		server->ds_wpages = server->wpages;
	}
}

/* Should the full nfs rpc cleanup code be used after io */
static int pnfs_use_rpc_code(struct pnfs_layoutdriver_type *ld)
{
	if (ld->ld_policy_ops->use_rpc_code)
		return ld->ld_policy_ops->use_rpc_code();
	else
		return 0;
}

/* Post-write completion function
 * Invoked by all layout drivers when write_pagelist is done.
 *
 * NOTE: callers set data->pnfsflags PNFS_NO_RPC
 * so that the NFS cleanup routines perform only the page cache
 * cleanup.
 */
static void
pnfs_writeback_done(struct nfs_write_data *data)
{
	dprintk("%s: Begin (status %d)\n", __func__, data->task.tk_status);

	/* update last write offset and need layout commit
	 * for non-files layout types (files layout calls
	 * pnfs4_write_done for this)
	 */
	if ((data->pnfsflags & PNFS_NO_RPC) &&
	    data->task.tk_status >= 0 && data->res.count > 0) {
		struct nfs_inode *nfsi = NFS_I(data->inode);

		pnfs_update_last_write(nfsi, data->args.offset, data->res.count);
		pnfs_need_layoutcommit(nfsi, data->args.context);
	}

	put_lseg(data->lseg);
	data->call_ops->rpc_call_done(&data->task, data);
	data->call_ops->rpc_release(data);
}

/*
 * return 0 for success, 1 for legacy nfs fallback, negative for error
 */
int
pnfs_flush_one(struct inode *inode, struct list_head *head,
		 unsigned int npages, size_t count, int how)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct layoutdriver_io_operations *io_ops;
	struct nfs_page *req;
	struct pnfs_layout_segment *lseg;
	int status;

	if (!pnfs_enabled_sb(nfss) || !nfss->pnfs_curr_ld->ld_io_ops->flush_one)
		goto fallback;

	req = nfs_list_entry(head->next);
	status = pnfs_update_layout(inode,
				    req->wb_context,
				    count,
				    req->wb_offset,
				    IOMODE_RW,
				    &lseg);
	if (status)
		goto fallback;
	io_ops = nfss->pnfs_curr_ld->ld_io_ops;
	status = io_ops->flush_one(lseg, head, npages, count, how);
	put_lseg(lseg);

	return status;
fallback:
	return nfs_flush_one(inode, head, npages, count, how);
}

/*
 * Obtain a layout for the the write range, and call do_sync_write.
 *
 * Unlike the read path which can wait until page coalescing
 * (pnfs_pageio_init_read) to get a layout, the write path discards the
 * request range to form the address_mapping - so we get a layout in
 * the file operations write method.
 *
 * If pnfs_update_layout fails, pages will be coalesced for MDS I/O.
 */
ssize_t
pnfs_file_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct nfs_open_context *context = filp->private_data;
	int status;

	if (!pnfs_enabled_sb(NFS_SERVER(inode)))
		goto out;

	/* Retrieve and set layout if not allready cached */
	status = pnfs_update_layout(inode,
				    context,
				    count,
				    *pos,
				    IOMODE_RW,
				    NULL);
	if (status) {
		dprintk("%s: Unable to get a layout for %Zd@%llu iomode %d)\n",
					__func__, count, *pos, IOMODE_RW);
	}
out:
	return do_sync_write(filp, buf, count, pos);
}

/*
 * Call the appropriate parallel I/O subsystem write function.
 * If no I/O device driver exists, or one does match the returned
 * fstype, then return a positive status for regular NFS processing.
 *
 * TODO: Is wdata->how and wdata->args.stable always the same value?
 * TODO: It seems in NFS, the server may not do a stable write even
 * though it was requested (and vice-versa?).  To check, it looks
 * in data->res.verf->committed.  Do we need this ability
 * for non-file layout drivers?
 */
static int
pnfs_writepages(struct nfs_write_data *wdata, int how)
{
	struct nfs_writeargs *args = &wdata->args;
	struct inode *inode = wdata->inode;
	int numpages, status;
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct pnfs_layout_segment *lseg;

	dprintk("%s: Writing ino:%lu %u@%llu\n",
		__func__,
		inode->i_ino,
		args->count,
		args->offset);

	/* Retrieve and set layout if not allready cached */
	status = pnfs_update_layout(inode,
				    args->context,
				    args->count,
				    args->offset,
				    IOMODE_RW,
				    &lseg);
	if (status) {
		status = 1;	/* retry with nfs I/O */
		goto out;
	}

	/* Determine number of pages
	 */
	numpages = nfs_page_array_len(args->pgbase, args->count);

	dprintk("%s: Calling layout driver (how %d) write with %d pages\n",
		__func__,
		how,
		numpages);
	if (!pnfs_use_rpc_code(nfss->pnfs_curr_ld))
		wdata->pnfsflags |= PNFS_NO_RPC;
	wdata->lseg = lseg;
	status = nfss->pnfs_curr_ld->ld_io_ops->write_pagelist(
							nfsi->current_layout,
							args->pages,
							args->pgbase,
							numpages,
							(loff_t)args->offset,
							args->count,
							how,
							wdata);

	BUG_ON(status < 0);
	if (status)
		wdata->pnfsflags &= ~PNFS_NO_RPC;
out:
	dprintk("%s: End Status %d\n", __func__, status);
	return status;
}

/* Post-read completion function.  Invoked by all layout drivers when
 * read_pagelist is done
 */
static void
pnfs_read_done(struct nfs_read_data *data)
{
	dprintk("%s: Begin (status %d)\n", __func__, data->task.tk_status);

	put_lseg(data->lseg);
	data->call_ops->rpc_call_done(&data->task, data);
	data->call_ops->rpc_release(data);
}

/*
 * Call the appropriate parallel I/O subsystem read function.
 * If no I/O device driver exists, or one does match the returned
 * fstype, then return a positive status for regular NFS processing.
 */
static int
pnfs_readpages(struct nfs_read_data *rdata)
{
	struct nfs_readargs *args = &rdata->args;
	struct inode *inode = rdata->inode;
	int numpages, status, pgcount, temp;
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct pnfs_layout_segment *lseg;

	dprintk("%s: Reading ino:%lu %u@%llu\n",
		__func__,
		inode->i_ino,
		args->count,
		args->offset);

	/* Retrieve and set layout if not allready cached */
	status = pnfs_update_layout(inode,
				    args->context,
				    args->count,
				    args->offset,
				    IOMODE_READ,
				    &lseg);
	if (status) {
		dprintk("%s: ERROR %d from pnfs_update_layout\n",
			__func__, status);
		status = 1;
		goto out;
	}

	/* Determine number of pages. */
	pgcount = args->pgbase + args->count;
	temp = pgcount % PAGE_CACHE_SIZE;
	numpages = pgcount / PAGE_CACHE_SIZE;
	if (temp != 0)
		numpages++;

	dprintk("%s: Calling layout driver read with %d pages\n",
		__func__, numpages);
	if (!pnfs_use_rpc_code(nfss->pnfs_curr_ld))
		rdata->pnfsflags |= PNFS_NO_RPC;
	rdata->lseg = lseg;
	status = nfss->pnfs_curr_ld->ld_io_ops->read_pagelist(
							nfsi->current_layout,
							args->pages,
							args->pgbase,
							numpages,
							(loff_t)args->offset,
							args->count,
							rdata);
	BUG_ON(status < 0);
	if (status)
		rdata->pnfsflags &= ~PNFS_NO_RPC;
 out:
	dprintk("%s: End Status %d\n", __func__, status);
	return status;
}

int _pnfs_try_to_read_data(struct nfs_read_data *data,
			   const struct rpc_call_ops *call_ops)
{
	struct inode *ino = data->inode;
	struct nfs_server *nfss = NFS_SERVER(ino);

	dprintk("--> %s\n", __func__);
	/* Only create an rpc request if utilizing NFSv4 I/O */
	if (!pnfs_use_read(ino, data->args.count) ||
	    !nfss->pnfs_curr_ld->ld_io_ops->read_pagelist) {
		dprintk("<-- %s: not using pnfs\n", __func__);
		return 1;
	} else {
		dprintk("%s: Utilizing pNFS I/O\n", __func__);
		data->call_ops = call_ops;
		return pnfs_readpages(data);
	}
}

int _pnfs_try_to_write_data(struct nfs_write_data *data,
			    const struct rpc_call_ops *call_ops, int how)
{
	struct inode *ino = data->inode;
	struct nfs_server *nfss = NFS_SERVER(ino);

	dprintk("--> %s\n", __func__);
	/* Only create an rpc request if utilizing NFSv4 I/O */
	if (!pnfs_use_write(ino, data->args.count) ||
	    !nfss->pnfs_curr_ld->ld_io_ops->write_pagelist) {
		dprintk("<-- %s: not using pnfs\n", __func__);
		return 1;
	} else {
		dprintk("%s: Utilizing pNFS I/O\n", __func__);
		data->call_ops = call_ops;
		data->how = how;
		return pnfs_writepages(data, how);
	}
}

int _pnfs_try_to_commit(struct nfs_write_data *data)
{
	struct inode *inode = data->inode;
	int status;

	if (!pnfs_use_write(inode, -1)) {
		dprintk("%s: Not using pNFS I/O\n", __func__);
		return 1;
	} else {
		/* data->call_ops and data->how set in nfs_commit_rpcsetup */
		dprintk("%s: Utilizing pNFS I/O\n", __func__);
		status = pnfs_commit(data, data->how);
		return status;
	}
}

/* pNFS Commit callback function for non-file layout drivers */
static void
pnfs_commit_done(struct nfs_write_data *data)
{
	dprintk("%s: Begin (status %d)\n", __func__, data->task.tk_status);

	put_lseg(data->lseg);
	data->call_ops->rpc_call_done(&data->task, data);
	data->call_ops->rpc_release(data);
}

int
pnfs_commit(struct nfs_write_data *data, int sync)
{
	int result = 0;
	struct nfs_inode *nfsi = NFS_I(data->inode);
	struct nfs_server *nfss = NFS_SERVER(data->inode);
	struct pnfs_layout_segment *lseg;
	struct nfs_page *first, *last, *p;
	int npages;

	dprintk("%s: Begin\n", __func__);

	/* If the layout driver doesn't define its own commit function
	 * use standard NFSv4 commit
	 */
	first = last = nfs_list_entry(data->pages.next);
	npages = 0;
	list_for_each_entry(p, &data->pages, wb_list) {
		last = p;
		npages++;
	}
	/* FIXME: we really ought to keep the layout segment that we used
	   to write the page around for committing it and never ask for a
	   new one.  If it was recalled we better commit the data first
	   before returning it, otherwise the data needs to be rewritten,
	   either with a new layout or to the MDS */
	result = pnfs_update_layout(data->inode,
				    NULL,
				    ((npages - 1) << PAGE_CACHE_SHIFT) +
				     first->wb_bytes +
				     (first != last) ? last->wb_bytes : 0,
				    first->wb_offset,
				    IOMODE_RW,
				    &lseg);
	/* If no layout have been retrieved,
	 * use standard NFSv4 commit
	 */
	if (result) {
		/* TODO: This doesn't match o_direct commit
		 * processing.  We need to align regular
		 * and o_direct commit processing.
		 */
		dprintk("%s: no layout. Not using pNFS.\n", __func__);
		return 1;
	}

	dprintk("%s: Calling layout driver commit\n", __func__);
	data->lseg = lseg;
	result = nfss->pnfs_curr_ld->ld_io_ops->commit(nfsi->current_layout,
						       sync, data);

	dprintk("%s end (err:%d)\n", __func__, result);
	return result;
}

/* Called on completion of layoutcommit */
void
pnfs_layoutcommit_done(
		struct pnfs_layoutcommit_data *data,
		int status)
{
	struct nfs_server *nfss = NFS_SERVER(data->inode);
	struct nfs_inode *nfsi = NFS_I(data->inode);

	dprintk("%s: (status %d)\n", __func__, status);

	/* TODO: For now, set an error in the open context (just like
	 * if a commit failed) We may want to do more, much more, like
	 * replay all writes through the NFSv4
	 * server, or something.
	 */
	if (status < 0) {
		printk(KERN_ERR "%s, Layoutcommit Failed! = %d\n",
		       __func__, status);
		data->ctx->error = status;
	}

	/* TODO: Maybe we should avoid this by allowing the layout driver
	 * to directly xdr its layout on the wire.
	 */
	if (nfss->pnfs_curr_ld->ld_io_ops->cleanup_layoutcommit)
		nfss->pnfs_curr_ld->ld_io_ops->cleanup_layoutcommit(
							nfsi->current_layout,
							&data->args,
							&data->res);

	/* release the open_context acquired in pnfs_writeback_done */
	put_nfs_open_context(data->ctx);
}

/* Called on completion of layoutcommit */
static void
pnfs_layoutcommit_rpc_done(struct rpc_task *task, void *calldata)
{
	pnfs_layoutcommit_done((struct pnfs_layoutcommit_data *)task->tk_calldata,
			       task->tk_status);
}

static int pnfs_layoutcommit_validate(struct rpc_task *task, void *data)
{
	struct pnfs_layoutcommit_data *ldata =
		(struct pnfs_layoutcommit_data *)data;
	struct nfs_server *server = NFS_SERVER(ldata->inode);

	return nfs4_setup_sequence(server->nfs_client, server->session,
		&ldata->args.seq_args, &ldata->res.seq_res, 1, task);
}

static const struct rpc_call_ops pnfs_layoutcommit_ops = {
	.rpc_call_done = pnfs_layoutcommit_rpc_done,
	.rpc_release = pnfs_layoutcommit_release,
	.rpc_call_validate_args = pnfs_layoutcommit_validate,
};

/* Execute a layoutcommit to the server */
static void
pnfs_execute_layoutcommit(struct pnfs_layoutcommit_data *data)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_PNFS_LAYOUTCOMMIT],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.task = &data->task,
		.rpc_client = NFS_CLIENT(data->inode),
		.rpc_message = &msg,
		.callback_ops = &pnfs_layoutcommit_ops,
		.callback_data = data,
		.flags = RPC_TASK_ASYNC,
	};
	struct rpc_task *task;

	dprintk("NFS: %4d initiating layoutcommit call. %llu@%llu lbw: %llu "
		"type: %d new_layout_size: %d\n",
		data->task.tk_pid,
		data->args.lseg.length,
		data->args.lseg.offset,
		data->args.lastbytewritten,
		data->args.layout_type,
		data->args.new_layout_size);

	task = rpc_run_task(&task_setup_data);
	if (!IS_ERR(task)) {
		dprintk("%s: rpc_run_task returned error %ld\n",
			__func__, PTR_ERR(task));
		rpc_put_task(task);
	}
}

/*
 * Set up the argument/result storage required for the RPC call.
 */
static int
pnfs_layoutcommit_setup(struct pnfs_layoutcommit_data *data, int sync)
{
	struct nfs_inode *nfsi = NFS_I(data->inode);
	struct nfs_server *nfss = NFS_SERVER(data->inode);
	int result = 0;

	dprintk("%s Begin (sync:%d)\n", __func__, sync);
	data->args.fh = NFS_FH(data->inode);
	data->args.layout_type = nfss->pnfs_curr_ld->id;

	/* Initialize new layout size.
	 * layout driver's setup_layoutcommit may optionally set
	 * the actual size of an updated layout.
	 */
	data->args.new_layout_size = 0;
	data->args.new_layout = NULL;

	/* TODO: Need to determine the correct values */
	data->args.time_modify_changed = 0;

	/* Set values from inode so it can be reset
	 */
	data->args.lseg.iomode = IOMODE_RW;
	data->args.lseg.offset = nfsi->pnfs_write_begin_pos;
	data->args.lseg.length = nfsi->pnfs_write_end_pos - nfsi->pnfs_write_begin_pos + 1;
	data->args.lastbytewritten = nfsi->pnfs_write_end_pos;
	data->args.bitmask = nfss->attr_bitmask;
	data->res.server = nfss;

	/* Call layout driver to set the arguments.
	 * TODO: We may want to avoid memory copies by delay this
	 * until xdr time.
	 */
	if (nfss->pnfs_curr_ld->ld_io_ops->setup_layoutcommit) {
		result = nfss->pnfs_curr_ld->ld_io_ops->setup_layoutcommit(
				nfsi->current_layout,
				&data->args);
		if (result)
			goto out;
	}

	data->res.fattr = &data->fattr;
	nfs_fattr_init(&data->fattr);

	if (sync)
		goto out;

out:
	dprintk("%s End Status %d\n", __func__, result);
	return result;
}

/* Issue a async layoutcommit for an inode.
 */
int
pnfs_layoutcommit_inode(struct inode *inode, int sync)
{
	struct pnfs_layoutcommit_data *data;
	struct nfs_inode *nfsi = NFS_I(inode);
	int status = 0;

	dprintk("%s Begin (sync:%d)\n", __func__, sync);

	data = pnfs_layoutcommit_alloc();
	if (!data)
		return -ENOMEM;

	spin_lock(&pnfs_spinlock);
	if (!nfsi->layoutcommit_ctx) {
		pnfs_layoutcommit_free(data);
		goto out_unlock;
	}

	data->inode = inode;
	data->cred  = nfsi->layoutcommit_ctx->cred;
	data->ctx = nfsi->layoutcommit_ctx;

	/* Set up layout commit args*/
	status = pnfs_layoutcommit_setup(data, sync);
	if (status)
		goto out_unlock;

	/* Clear layoutcommit properties in the inode so
	 * new lc info can be generated
	 */
	nfsi->pnfs_write_begin_pos = 0;
	nfsi->pnfs_write_end_pos = 0;
	nfsi->layoutcommit_ctx = NULL;

	/* release lock on pnfs layoutcommit attrs */
	spin_unlock(&pnfs_spinlock);

	/* Execute the layout commit synchronously */
	if (sync) {
		status = NFS_PROTO(inode)->pnfs_layoutcommit(data);
		pnfs_layoutcommit_done(data, status);
	} else {
		pnfs_execute_layoutcommit(data);
	}
out:
	dprintk("%s end (err:%d)\n", __func__, status);
	return status;
out_unlock:
	spin_unlock(&pnfs_spinlock);
	goto out;
}

void pnfs_free_request_data(struct nfs_page *req)
{
	struct layoutdriver_io_operations *lo;

	if (!req->wb_ops || !req->wb_private)
		return;
	lo = (struct layoutdriver_io_operations *)req->wb_ops;
	if (lo->free_request_data)
		lo->free_request_data(req);
}

/* Callback operations for layout drivers.
 */
struct pnfs_client_operations pnfs_ops = {
	.nfs_getdevicelist = nfs4_pnfs_getdevicelist,
	.nfs_getdeviceinfo = nfs4_pnfs_getdeviceinfo,
	.nfs_readlist_complete = pnfs_read_done,
	.nfs_writelist_complete = pnfs_writeback_done,
	.nfs_commit_complete = pnfs_commit_done,
};

EXPORT_SYMBOL(pnfs_unregister_layoutdriver);
EXPORT_SYMBOL(pnfs_register_layoutdriver);

#endif /* CONFIG_PNFS */
