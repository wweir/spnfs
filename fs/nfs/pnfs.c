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

	if (!pnfs_initialized) {
		printk(KERN_ERR "%s Registration failure. "
		       "pNFS not initialized.\n", __func__);
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
	   struct nfs4_pnfs_layoutget_arg *arg,
	   struct nfs4_pnfs_layoutget_res *res)
{
	int status;
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_pnfs_layoutget gdata = {
		.args = arg,
		.res = res,
	};
	dprintk("%s:Begin\n", __func__);

	arg->type = server->pnfs_curr_ld->id;
	arg->minlength = arg->lseg.length;
	arg->maxcount = PNFS_LAYOUT_MAXSIZE;
	arg->inode = ino;
	arg->ctx = ctx;

	/* Retrieve layout information from server */
	status = NFS_PROTO(ino)->pnfs_layoutget(&gdata);
	return status;
}

int
pnfs_return_layout(struct inode *ino, struct nfs4_pnfs_layout_segment *range)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_pnfs_layoutreturn_arg arg;
	int status;

	dprintk("%s:Begin layout %p\n", __func__, nfsi->current_layout);

	if (nfsi->current_layout == NULL)
		return 0;

	arg.reclaim = 0;
	arg.layout_type = server->pnfs_curr_ld->id;
	arg.return_type = RETURN_FILE;
	if (range)
		arg.lseg = *range;
	else {
		arg.lseg.iomode = IOMODE_ANY /* for now */;
		arg.lseg.offset = 0;
		arg.lseg.length = ~0;
	}
	arg.inode = ino;

	status = pnfs_return_layout_rpc(server, &arg);

	if (nfsi->current_layout) {
		if (status)
			dprintk("%s: pnfs_return_layout_rpc status=%d. "
				"removing layout anyway\n", __func__,
				status);
		else
			dprintk("%s: removing layout\n", __func__);

		server->pnfs_curr_ld->ld_io_ops->free_layout(
			&nfsi->current_layout, ino, &arg.lseg);
	}

	dprintk("%s:Exit status %d\n", __func__, status);
	return status;
}

int
pnfs_return_layout_rpc(struct nfs_server *server,
			struct nfs4_pnfs_layoutreturn_arg *argp)
{
	int status;
	struct nfs4_pnfs_layoutreturn_res res;
	struct nfs4_pnfs_layoutreturn gdata = {
		.args = argp,
		.res = &res,
	};
	dprintk("%s:Begin\n", __func__);

	/* XXX Need to setup the sequence */
/*
	status = server->nfs_client->rpc_ops->setup_sequence(
				server->session,
				argp->minorversion_info,
				res.minorversion_info)
	if (status)
			goto out;
*/
	/* Return layout to server */
	status = server->nfs_client->rpc_ops->pnfs_layoutreturn(&gdata);

/*
	server->nfs_client->rpc_ops->sequence_done(server->session,
				res.minorversion_info, status);

out:
*/
	dprintk("%s:Exit status %d\n", __func__, status);
	return status;
}

/* DH: Inject layout blob into the I/O module.  This must happen before
 *     the I/O module has its read/write methods called.
 */
static struct pnfs_layout_type *
pnfs_inject_layout(struct nfs_inode *nfsi,
		   struct layoutdriver_io_operations *io_ops,
		   struct nfs4_pnfs_layoutget_res *lgr)
{
	struct pnfs_layout_type *layid;
	struct inode *inode = &nfsi->vfs_inode;
	struct nfs_server *server = NFS_SERVER(inode);

	dprintk("%s Begin\n", __func__);

	if (!io_ops->alloc_layout || !io_ops->set_layout) {
		printk(KERN_ERR "%s ERROR! Layout driver lacking pNFS layout ops!!!\n", __func__);
		return NULL;
	}

	if (nfsi->current_layout == NULL) {
		dprintk("%s Alloc'ing layout\n", __func__);
		layid = io_ops->alloc_layout(server->pnfs_mountid, inode);
	} else {
		dprintk("%s Adding to current layout\n", __func__);
		layid = nfsi->current_layout;
	}

	if (!layid) {
		printk(KERN_ERR "%s ERROR! Layout id non-existent!!!\n",
		       __func__);
		return NULL;
	}
	dprintk("%s Calling set layout\n", __func__);
	return io_ops->set_layout(layid, inode, lgr);
}

/* Check to see if the module is handling which layouts need to be
 * retrieved from the server.  If they are not, then use retrieve based
 * upon the returned data ranges from get_layout.
 */
int
virtual_update_layout(struct inode *ino,
		      struct nfs_open_context *ctx,
		      size_t count,
		      loff_t pos,
		      enum pnfs_iomode iomode)
{
	struct nfs4_pnfs_layoutget_res res;
	struct nfs4_pnfs_layoutget_arg arg;
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_server *nfss = NFS_SERVER(ino);
	struct pnfs_layout_type *layout_new;
	int result = -EIO;

	/* TODO: Check to see if the pnfs module is handling data layout
	 * range caching Something like:
	 * return(nfss->pnfs_module->pnfs_io_interface->have_layout(..))
	 */

	arg.lseg.iomode = iomode;
	arg.lseg.offset = pos;
	arg.lseg.length = count;
	/* Check to see if the layout for the given range already exists */
	if (nfsi->current_layout != NULL &&
	    (!nfss->pnfs_curr_ld->ld_io_ops->has_layout ||
	      nfss->pnfs_curr_ld->ld_io_ops->has_layout(
		nfsi->current_layout, ino, &arg.lseg))) {
		/* TODO: To make this generic, I would need to compare the extents
		 * of the existing layout information.
		 * For now, assume that whole file layouts are always returned.
		 */
		dprintk("%s: Using cached layout %p for %llu@%llu iomode %d)\n",
			__func__,
			nfsi->current_layout,
			arg.lseg.length,
			arg.lseg.offset,
			arg.lseg.iomode);

		return 0; /* Already have layout information */
	}

	res.layout.buf = NULL;

	/* if get layout already failed once goto out */
	if (nfsi->pnfs_layout_state & NFS_INO_LAYOUT_FAILED) {
		if (unlikely(nfsi->pnfs_layout_suspend &&
		    get_seconds() >= nfsi->pnfs_layout_suspend)) {
			dprintk("%s: layout_get resumed\n", __func__);
			nfsi->pnfs_layout_state &= ~NFS_INO_LAYOUT_FAILED;
			nfsi->pnfs_layout_suspend = 0;
		} else
			result = 1;
		goto out;
	}

	result = get_layout(ino, ctx, &arg, &res);
	if (result) {
		printk(KERN_ERR "%s: ERROR retrieving layout %d\n",
		       __func__, result);

		switch (result) {
		case -ENOENT:	/* NFS4ERR_BADLAYOUT */
			/* transient error, don't mark with NFS_INO_LAYOUT_FAILED */
			result = 1;
			break;

		case -EAGAIN:	/* NFS4ERR_LAYOUTTRYLATER, NFS4ERR_RECALLCONFLICT, NFS4ERR_LOCKED */
			nfsi->pnfs_layout_suspend = get_seconds() + 1;
			dprintk("%s: layout_get suspended until %ld\n",
				__func__, nfsi->pnfs_layout_suspend);
			break;
		case -EINVAL:	/* NFS4ERR_INVAL, NFSERR_BADIOMODE, NFS4ERR_UNKNOWN_LAYOUTTYPE */
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
		goto out;
	}

	if (res.layout.len <= 0) {
		printk(KERN_ERR
		       "%s: ERROR!  Layout size is ZERO!\n", __func__);
		result =  -EIO;
		goto out;
	}

	/* Inject layout blob into I/O device driver */
	layout_new = pnfs_inject_layout(nfsi,
					nfss->pnfs_curr_ld->ld_io_ops,
					&res);
	if (layout_new == NULL) {
		printk(KERN_ERR "%s: ERROR!  Could not inject layout (%d)\n",
		       __func__, result);
		result =  -EIO;
		goto out;
	}

	if (res.return_on_close) {
		layout_new->roc_iomode |= res.lseg.iomode;
		if (!layout_new->roc_iomode)
			layout_new->roc_iomode = IOMODE_ANY;
	}
	nfsi->current_layout = layout_new;

	result = 0;
out:

	/* remember that get layout failed and don't try again */
	if (result < 0)
		nfsi->pnfs_layout_state |= NFS_INO_LAYOUT_FAILED;

	/* res.layout.buf kalloc'ed by the xdr decoder? */
	kfree(res.layout.buf);
	dprintk("%s end (err:%d) state %d\n",
		__func__, result, nfsi->pnfs_layout_state);
	return result;
}

/* Return true if a layout driver is being used for this mountpoint */
int
pnfs_enabled_sb(struct nfs_server *nfss)
{
	if (!nfss->pnfs_curr_ld)
		return 0;

	return 1;
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
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	ssize_t threshold = -1;

	if (!pnfs_enabled_sb(nfss) ||
	    !nfss->pnfs_curr_ld->ld_policy_ops)
		return 0;

	if (iswrite && nfss->pnfs_curr_ld->ld_policy_ops->get_write_threshold) {
		threshold = nfss->pnfs_curr_ld->ld_policy_ops->get_write_threshold(nfsi->current_layout, inode);
		dprintk("%s wthresh: %Zd\n", __func__, threshold);
		goto check;
	}

	if (!iswrite && nfss->pnfs_curr_ld->ld_policy_ops->get_read_threshold) {
		threshold = nfss->pnfs_curr_ld->ld_policy_ops->get_read_threshold(nfsi->current_layout, inode);
		dprintk("%s rthresh: %Zd\n", __func__, threshold);
	}

check:
	if ((ssize_t)req_size <= threshold)
		return 1;
	else
		return 0;
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
	lo = nfsi->current_layout;
	if (lo)
		stripe_size = policy_ops->get_stripesize(lo, inode);
out:
	return stripe_size;
}

/*
 * rsize is already set by caller to MDS rsize.
 */
void
pnfs_set_ds_rsize(struct inode *inode,
		  struct nfs_open_context *ctx,
		  struct list_head *pages,
		  unsigned long nr_pages,
		  loff_t offset,
		  size_t *rsize,
		  struct nfs_pageio_descriptor *pgio)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	loff_t end_offset, i_size;
	size_t count;
	int status = 0;

	dprintk("--> %s inode %p ctx %p pages %p nr_pages %lu offset %lu\n",
		__func__, inode, ctx, pages, nr_pages,(unsigned long)offset);

	pgio->pg_boundary = 0;
	pgio->pg_test = 0;

	if (!pnfs_enabled_sb(nfss))
		return;

	/* Calculate the total read-ahead count */
	end_offset = (offset & PAGE_CACHE_MASK) + nr_pages * PAGE_CACHE_SIZE;
	i_size = i_size_read(inode);
	if (end_offset > i_size)
		end_offset = i_size;
	count = end_offset - offset;

	dprintk("%s count %ld\n", __func__,(long int)count);


	status = virtual_update_layout(inode, ctx, count,
						offset, IOMODE_READ);
	dprintk("%s *rsize %Zd virt update returned %d\n",
					__func__, *rsize, status);

	if (status == 0 && count > 0 && !below_threshold(inode, count, 0))
		*rsize = NFS_SERVER(inode)->ds_rsize;

	/* boundary set => gather pages by stripe => need pg_test */
	pgio->pg_boundary = pnfs_getboundary(inode);
	if (pgio->pg_boundary)
		pnfs_set_pg_test(inode, pgio);

	dprintk("<-- %s pg_boundary %d, pg_test %p\n", __func__,
			pgio->pg_boundary, pgio->pg_test);
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

/* Retrieve layout driver type id */
static int
pnfs_get_type(struct inode *inode)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	if (!pnfs_enabled_sb(nfss))
		return 0;
	return nfss->pnfs_curr_ld->id;
}

/* Determine if the the NFSv4 protocol is to be used for writes,
 * whether pNFS is being used or not.
 * TODO: Instead of checking for the file layout type, maybe
 * we should make this a policy option in the future if more
 * layout drivers uses NFSv4 I/O.
 */
int
pnfs_use_nfsv4_wproto(struct inode *inode, ssize_t count)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	if (!pnfs_enabled_sb(nfss) ||
	    pnfs_get_type(inode) == LAYOUT_NFSV4_FILES ||
	    !pnfs_use_write(inode, count))
		return 1;

	return 0;
}

/* Determine if the the NFSv4 protocol is to be used for reads,
 * whether pNFS is being used or not.
 * TODO: See pnfs_use_nfsv4_wproto.
 */
int
pnfs_use_nfsv4_rproto(struct inode *inode, ssize_t count)
{
	struct nfs_server *nfss = NFS_SERVER(inode);
	if (!pnfs_enabled_sb(nfss) ||
	    pnfs_get_type(inode) == LAYOUT_NFSV4_FILES ||
	    !pnfs_use_read(inode, count))
		return 1;

	return 0;
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

/* Invoked by all non-NFSv4 I/O layout drivers to mark pages for commit
 */
static void
pnfs_writeback_done(struct nfs_write_data *data, ssize_t status)
{
	dprintk("%s: Begin (status %Zd)\n", __func__, status);

	/* NFSv4 will have sunrpc call the callbacks */
	if (data->call_ops == NULL ||
	    pnfs_use_nfsv4_wproto(data->inode, data->args.count))
		return;

	/* Status is the number of bytes written or an error code */
	data->task.tk_status = status;
	data->res.count = status;
	pnfs_writeback_done_norpc(&data->task, data);
	data->call_ops->rpc_release(data);
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
	int numpages, status, pgcount, temp;
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);

	dprintk("%s: Writing ino:%lu %u@%llu\n",
		__func__,
		inode->i_ino,
		args->count,
		args->offset);

	/* Retrieve and set layout if not allready cached */
	status = virtual_update_layout(inode,
				       args->context,
				       args->count,
				       args->offset,
				       IOMODE_RW);
	if (status) {
		status = 1;	/* retry with nfs I/O */
		goto out;
	}

	if (!nfss->pnfs_curr_ld->ld_io_ops ||
	    !nfss->pnfs_curr_ld->ld_io_ops->write_pagelist) {
		printk(KERN_ERR
		       "%s: ERROR, no layout driver write operation\n",
		       __func__);
		status = 1;
		goto out;
	}

	/* Determine number of pages
	 */
	pgcount = args->pgbase + args->count;
	temp = pgcount % PAGE_CACHE_SIZE;
	numpages = pgcount / PAGE_CACHE_SIZE;
	if (temp != 0)
		numpages++;

	dprintk("%s: Calling layout driver (how %d) write with %d pages\n",
		__func__,
		how,
		numpages);
	if (pnfs_get_type(inode) != LAYOUT_NFSV4_FILES)
		wdata->pnfsflags |= PNFS_NO_RPC;
	status = nfss->pnfs_curr_ld->ld_io_ops->write_pagelist(nfsi->current_layout,
							       inode,
							       args->pages,
							       args->pgbase,
							       numpages,
							       (loff_t)args->offset,
							       args->count,
							       how,
							       wdata);

	if (status > 0) {
		dprintk("%s: LD write_pagelist returned status %d > 0\n", __func__, status);
		pnfs_update_last_write(nfsi, args->offset, status);
		pnfs_need_layoutcommit(nfsi, wdata->args.context);
		status = 0;
	}

out:
	dprintk("%s: End Status %d\n", __func__, status);
	return status;
}

/* Post-read completion function.  Invoked by non RPC layout drivers
 * to clean up read pages.
 *
 * NOTE: called must set data->pnfsflags PNFS_NO_RPC
 */
static void
pnfs_read_done(struct nfs_read_data *data, ssize_t status, int eof)
{
	dprintk("%s: Begin (status %Zd)\n", __func__, status);

	/* NFSv4 will have sunrpc call the callbacks */
	if (data->call_ops == NULL ||
	    pnfs_use_nfsv4_rproto(data->inode, data->args.count))
		return;

	/* Status is the number of bytes written or an error code
	 * the rpc_task is uninitialized, and tk_status is all that
	 * is used in the call done routines.
	 */
	data->task.tk_status = status;
	data->res.eof = eof;
	data->res.count = status;

	/* call the NFS cleanup routines. */
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

	dprintk("%s: Reading ino:%lu %u@%llu\n",
		__func__,
		inode->i_ino,
		args->count,
		args->offset);

	/* Retrieve and set layout if not allready cached */
	status = virtual_update_layout(inode,
				       args->context,
				       args->count,
				       args->offset,
				       IOMODE_READ);
	if (status) {
		printk(KERN_WARNING
		       "%s: ERROR %d from virtual_update_layout\n",
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

	dprintk("%s: Calling layout driver read with %d pages\n", __func__, numpages);
	if (pnfs_get_type(inode) != LAYOUT_NFSV4_FILES)
		rdata->pnfsflags |= PNFS_NO_RPC;
	status = nfss->pnfs_curr_ld->ld_io_ops->read_pagelist(nfsi->current_layout,
							      inode,
							      args->pages,
							      args->pgbase,
							      numpages,
							      (loff_t)args->offset,
							      args->count,
							      rdata);
	if (status > 0) {
		dprintk("%s: LD read_pagelist returned status %d > 0\n", __func__, status);
		status = 0;
	}

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

int pnfs_try_to_write_data(struct nfs_write_data *data,
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

int pnfs_try_to_commit(struct inode *inode, struct nfs_write_data *data, struct list_head *head, int how)
{
	int status;

	dprintk("%s:Begin\n", __func__);
	if (!pnfs_use_write(inode, -1)) {
		dprintk("%s:End not using pnfs\n", __func__);
		return 1;
	} else {
		/* data->call_ops already set in nfs_commit_rpcsetup */
		dprintk("%s Utilizing pNFS I/O\n", __func__);
		status = pnfs_commit(inode, head, how, data);
		if (status < 0)
			return status;
		return 0;
	}
}

/* pNFS Commit callback function for non-file layout drivers */
static void
pnfs_commit_done(struct nfs_write_data *data, ssize_t status)
{
	dprintk("%s: Begin (status %Zd)\n", __func__, status);

	/* NFSv4 will have sunrpc call the callbacks */
	if (pnfs_use_nfsv4_wproto(data->inode, -1))
		return;

	/* Status is the number of bytes written or an error code */
	data->task.tk_status = status;
	pnfs_commit_done_norpc(&data->task, data);
	data->call_ops->rpc_release(data);
}

int
pnfs_commit(struct inode *inode,
	    struct list_head *head,
	    int sync,
	    struct nfs_write_data *data)
{
	int result = 0;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_server *nfss = NFS_SERVER(inode);
	dprintk("%s: Begin\n", __func__);

	/* If the layout driver doesn't define its own commit function
	 * OR no layout have been retrieved,
	 * use standard NFSv4 commit
	 */
	if (!nfsi->current_layout ||
	    !nfss->pnfs_curr_ld->ld_io_ops->commit) {
		/* TODO: This doesn't match o_direct commit
		 * processing.  We need to align regular
		 * and o_direct commit processing.
		 */
		dprintk("%s: Not using pNFS\n", __func__);
		return 1;
	}

	dprintk("%s: Calling layout driver commit\n", __func__);
	result = nfss->pnfs_curr_ld->ld_io_ops->commit(nfsi->current_layout,
						       inode, head, sync, data);

	dprintk("%s end (err:%d)\n", __func__, result);
	return result;
}

int
pnfs_getdevicelist(struct super_block *sb, struct nfs_fh *fh,
		   struct pnfs_devicelist *devlist)
{
	struct nfs_server *server = NFS_SB(sb);

	return nfs4_pnfs_getdevicelist(fh, server, devlist);
}

/* Retrieve the device information for a device.
 */
int
pnfs_getdeviceinfo(struct inode *inode, u32 dev_id, struct pnfs_device *dev)
{
	int rc;

	rc = nfs4_pnfs_getdeviceinfo(inode, dev_id, dev);

	return rc;
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
		nfss->pnfs_curr_ld->ld_io_ops->cleanup_layoutcommit(nfsi->current_layout,
								    data->inode,
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

static const struct rpc_call_ops pnfs_layoutcommit_ops = {
	.rpc_call_done = pnfs_layoutcommit_rpc_done,
	.rpc_release = pnfs_layoutcommit_release,
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

	/* TODO: Need to determine the correct values */
	data->args.time_modify_changed = 0;
	data->args.time_access_changed = 0;

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
				data->inode,
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

/* Callback operations for layout drivers.
 */
struct pnfs_client_operations pnfs_ops = {
	.nfs_getdevicelist = pnfs_getdevicelist,
	.nfs_getdeviceinfo = pnfs_getdeviceinfo,
	.nfs_readlist_complete = pnfs_read_done,
	.nfs_writelist_complete = pnfs_writeback_done,
	.nfs_commit_complete = pnfs_commit_done,
};

int
pnfs_wsize(struct inode *inode, unsigned int count, struct nfs_write_data *wdata)
{
	if (count >= 0 && below_threshold(inode, count, 1))
		return NFS_SERVER(inode)->wsize;

	return NFS_SERVER(inode)->ds_wsize;
}

/*
 * pnfs_rpages, pnfs_wpages.
 *
 * TODO:  We have a chicken and egg problem since
 * at the point that we call the pnfs_rpages or pnfs_wpages,
 *  we don't know the size of the request, and so
 * we can't determine if we are using pNFS or NFSv4, so we
 * can't determine if we should use the ds_wpages or the w_pages
 * value.  Ensure that if you are setting your blocksize (wsize) larger
 * than what the MDS can support, you set your write threshold to
 * a maximum value of the MDS wsize.
 */
int
pnfs_rpages(struct inode *inode)
{
	return NFS_SERVER(inode)->ds_rpages;
}

int
pnfs_wpages(struct inode *inode)
{
	return NFS_SERVER(inode)->ds_wpages;
}

EXPORT_SYMBOL(pnfs_unregister_layoutdriver);
EXPORT_SYMBOL(pnfs_register_layoutdriver);

#endif /* CONFIG_PNFS */
