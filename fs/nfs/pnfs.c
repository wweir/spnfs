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
#include <linux/nfs4_pnfs.h>

#include "nfs4_fs.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS

#define MIN_POOL_LC		(4)

extern int nfs_fsync(struct file *file, struct dentry *dentry, int datasync);
extern int nfs4_pnfs_getdevicelist(struct nfs_server *server, struct pnfs_devicelist* devlist);
extern int nfs4_pnfs_getdeviceinfo(struct nfs_server *server, u32 dev_id, struct pnfs_device *res);
extern void nfs_execute_write(struct nfs_write_data *data);
extern void nfs_commit_rpcsetup(struct nfs_write_data *data, int sync);

struct pnfs_client_operations pnfs_ops;

static int pnfs_initialized = 0;

/* Locking:
 *
 * pnfs_spinlock:
 * 	protects pnfs_modules_tbl.
 */
static spinlock_t pnfs_spinlock = SPIN_LOCK_UNLOCKED;

/*
 * pnfs_modules_tbl holds all pnfs modules
 */
static struct list_head	pnfs_modules_tbl;
static kmem_cache_t *pnfs_cachep;
static mempool_t *pnfs_layoutcommit_mempool;

static inline struct pnfs_layoutcommit_data *pnfs_layoutcommit_alloc(void)
{
	struct pnfs_layoutcommit_data *p = mempool_alloc(pnfs_layoutcommit_mempool,
							 SLAB_NOFS);
	if (p)
		memset(p, 0, sizeof(*p));
	else
		goto out;

	p->args.minorversion_info = kzalloc(sizeof(struct nfs41_sequence_args), GFP_KERNEL);
	if (!p->args.minorversion_info)
		goto out_free1;

	p->res.minorversion_info = kzalloc(sizeof(struct nfs41_sequence_res), GFP_KERNEL);
	if (!p->res.minorversion_info)
		goto out_free2;

	return p;

out_free2:
	kfree(p->args.minorversion_info);
out_free1:
	mempool_free(p, pnfs_layoutcommit_mempool);
out:
	return NULL;
}

static inline void pnfs_layoutcommit_free(struct pnfs_layoutcommit_data *p)
{
	kfree(p->args.minorversion_info);
	kfree(p->res.minorversion_info);
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
					0, SLAB_HWCACHE_ALIGN,
					NULL, NULL);
	if (pnfs_cachep == NULL)
		return -ENOMEM;

	pnfs_layoutcommit_mempool = mempool_create(MIN_POOL_LC,
						   mempool_alloc_slab,
						   mempool_free_slab,
						   pnfs_cachep);
	if (pnfs_layoutcommit_mempool == NULL)
		return -ENOMEM;

	pnfs_initialized = 1;
	return 0;
}

void pnfs_uninitialize(void)
{
	mempool_destroy(pnfs_layoutcommit_mempool);
	if (kmem_cache_destroy(pnfs_cachep))
		printk(KERN_INFO "%s: not all structures were freed\n", __FUNCTION__);
}

/* search pnfs_modules_tbl for right pnfs module */
static int
find_pnfs(int id, struct pnfs_module **module) {
	struct  pnfs_module* local = NULL;

	dprintk("PNFS: %s: Searching for %d\n",__func__, id);
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
pnfs_need_layoutcommit(struct nfs_inode* nfsi, struct nfs_open_context *ctx)
{
	dprintk("%s: current_layout=%p layoutcommit_ctx=%p ctx=%p\n",__FUNCTION__,
		nfsi->current_layout, nfsi->layoutcommit_ctx, ctx);
	spin_lock(&pnfs_spinlock);
	if (nfsi->current_layout && !nfsi->layoutcommit_ctx) {
		nfsi->layoutcommit_ctx = get_nfs_open_context(ctx);
		nfsi->change_attr++;
		spin_unlock(&pnfs_spinlock);
		dprintk("%s: Set layoutcommit_ctx=%p\n",__FUNCTION__, nfsi->layoutcommit_ctx);
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
pnfs_update_last_write(struct nfs_inode* nfsi, loff_t offset, size_t extent) {
	loff_t end_pos, orig_offset = offset;

	if (orig_offset < nfsi->pnfs_write_begin_pos)
		nfsi->pnfs_write_begin_pos = orig_offset;
	end_pos = orig_offset + extent - 1; /* I'm being inclusive */
	if (end_pos > nfsi->pnfs_write_end_pos)
		nfsi->pnfs_write_end_pos = end_pos;
	dprintk("%s: Wrote %lu@%lu bpos %lu, epos: %lu\n",
		__FUNCTION__,
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
		server->pnfs_curr_ld->ld_io_ops->uninitialize_mountpoint(server->pnfs_mountid);
}

/*
 * Set the server pnfs module to the first registered pnfs_type.
 * Only one pNFS layout driver is supported.
 */
void
set_pnfs_layoutdriver(struct super_block *sb, u32 id)
{
	struct pnfs_module *mod;
	struct pnfs_mount_type* mt;
	struct nfs_server *server = NFS_SB(sb);

	if (id > 0 &&
	    find_pnfs(id, &mod)) {
		dprintk("%s: Setting pNFS module\n",__FUNCTION__);
		server->pnfs_curr_ld = mod->pnfs_ld_type;
		mt = server->pnfs_curr_ld->ld_io_ops->initialize_mountpoint(sb);
		if (!mt) {
			printk("%s: Error initializing mount point for layout driver %d. ",__FUNCTION__, id);
			goto out_err;
		}
		/* Layout driver succeeded in initializing mountpoint */
		server->pnfs_mountid = mt;
		/* Set the rpc_ops */
		server->rpc_ops = &pnfs_v4_clientops;
		return;
	}

	dprintk("%s: No pNFS module found for %d. ",__FUNCTION__, id);
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
		printk("%s Registration failure.  pNFS not initialized.\n",__FUNCTION__);
		return NULL;
	}

	if ((pnfs_mod = kmalloc(sizeof(struct pnfs_module), GFP_KERNEL))!= NULL) {
		dprintk("%s Registering id:%d name:%s\n",
			__FUNCTION__,
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
		dprintk("%s Deregistering id:%d\n",__FUNCTION__, ld_type->id);
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
get_layout(struct inode* ino,
	   struct nfs_open_context* ctx,
	   size_t count, loff_t pos,
	   enum pnfs_iomode iomode,
	   struct nfs4_pnfs_layoutget_arg* arg,
	   struct nfs4_pnfs_layoutget_res* res)
{
	int status;
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_pnfs_layoutget gdata = {
		.args = arg,
		.res = res,
	};
	dprintk("%s:Begin\n",__FUNCTION__);

	arg->type = server->pnfs_curr_ld->id;
	arg->iomode = iomode;
	arg->offset = 0;
	arg->length = (__u64)~0;
	arg->minlength = count;
	arg->maxcount = PNFS_LAYOUT_MAXSIZE;
	arg->inode = ino;
	arg->ctx = ctx;

	/* Retrieve layout information from server */
	status = NFS_PROTO(ino)->pnfs_layoutget(&gdata);
	return status;
}

int
pnfs_return_layout(struct inode* ino)
{
	struct nfs4_pnfs_layoutreturn_arg arg;
	struct nfs4_pnfs_layoutreturn_res res;
	struct nfs41_sequence_args seqarg;
	struct nfs41_sequence_res seqres;
	int status;

	struct nfs_inode* nfsi = NFS_I(ino);
	struct nfs_server *server = NFS_SERVER(ino);
	struct nfs4_pnfs_layoutreturn gdata = {
		.args = &arg,
		.res = &res,
	};
	dprintk("%s:Begin layout %p\n", __FUNCTION__, nfsi->current_layout);

	if (nfsi->current_layout == NULL)
		return 0;

	dprintk("%s: Returning layout...\n", __FUNCTION__);

	arg.reclaim = 0;
	arg.layout_type = server->pnfs_curr_ld->id;
	arg.iomode = IOMODE_ANY /* for now */;
	arg.return_type = LAYOUTRETURN_FILE;
	arg.offset = 0;
	arg.length = ~0;
	arg.inode = ino;
	arg.minorversion_info = &seqarg;
	res.minorversion_info = &seqres;

	if ((status = server->rpc_ops->setup_sequence(server->nfs4_state->cl_session,
					arg.minorversion_info,
					res.minorversion_info)))
			goto out;

	/* Return layout to server */
	status = NFS_PROTO(ino)->pnfs_layoutreturn(&gdata);

	server->rpc_ops->sequence_done(server->nfs4_state->cl_session,
				res.minorversion_info, status);

	if (!status) {
		dprintk ("%s: removing layout\n", __FUNCTION__);
		server->pnfs_curr_ld->ld_io_ops->free_layout(NFS_I(ino)->current_layout, ino, 0, 0);
		nfsi->current_layout = NULL;
	}

	dprintk("%s:Exit status %d\n", __FUNCTION__, status);
out:
	return status;
}

/* DH: Inject layout blob into the I/O module.  This must happen before
 *     the I/O module has its read/write methods called.
 */
static struct pnfs_layout_type*
pnfs_inject_layout(struct nfs_inode* nfsi,
		   struct layoutdriver_io_operations* io_ops,
		   void* new_layout)
{
	struct pnfs_layout_type *layid;
	struct inode* inode = &nfsi->vfs_inode;
	struct nfs_server *server = NFS_SERVER(inode);

	dprintk("%s Begin\n",__FUNCTION__);

	if (!io_ops->alloc_layout || !io_ops->set_layout) {
		printk("%s ERROR! Layout driver lacking pNFS layout ops!!!\n",__FUNCTION__);
		return NULL;
	}

	if (nfsi->current_layout == NULL) {
		dprintk("%s Alloc'ing layout\n",__FUNCTION__);
		layid = io_ops->alloc_layout(server->pnfs_mountid, inode);
	} else {
		dprintk("%s Adding to current layout\n",__FUNCTION__);
		layid = nfsi->current_layout;
	}

	if (!layid) {
		printk("%s ERROR! Layout id non-existent!!!\n",__FUNCTION__);
		return NULL;
	}
	dprintk("%s Calling set layout\n",__FUNCTION__);
	return io_ops->set_layout(layid, inode, (void*)new_layout);
}

/* Check to see if the module is handling which layouts need to be
 * retrieved from the server.  If they are not, then use retrieve based
 * upon the returned data ranges from get_layout.
 */
int
virtual_update_layout(struct inode* ino,
		      struct nfs_open_context* ctx,
		      size_t count,
		      loff_t pos,
		      enum pnfs_iomode iomode)
{
	struct nfs4_pnfs_layoutget_res res;
	struct nfs4_pnfs_layoutget_arg arg;
	struct nfs_inode* nfsi = NFS_I(ino);
	struct nfs_server* nfss = NFS_SERVER(ino);
	struct pnfs_layout_type* layout_new;
	int result = -EIO;

	/* TODO: Check to see if the pnfs module is handling data layout
	 * range caching Something like:
	 *return(nfss->pnfs_module->pnfs_io_interface->have_layout(..))
	 */

	/* Check to see if the layout for the given range already exists */
	if (nfsi->current_layout != NULL) {
		/* TODO: To make this generic, I would need to compare the extents
		 * of the existing layout information.
		 * For now, assume that whole file layouts are always returned.
		 */
		dprintk("%s: Using cached layout for %lu@%lu)\n",
			__FUNCTION__,
			(unsigned long)count,
			(unsigned long)pos);

		return 0; /* Already have layout information */
	}

	res.layout.buf = NULL;

        /* if get layout already failed once goto out */
	if (nfsi->pnfs_layout_state & NFS_INO_LAYOUT_FAILED) {
		if (unlikely(nfsi->pnfs_layout_suspend &&
		             get_seconds() >= nfsi->pnfs_layout_suspend)) {
			dprintk("%s: layout_get resumed\n", __FUNCTION__);
			nfsi->pnfs_layout_state &= ~NFS_INO_LAYOUT_FAILED;
			nfsi->pnfs_layout_suspend = 0;
		} else
			result = 1;
		goto out;
	}

	if ((result = get_layout(ino, ctx, count, pos, iomode, &arg, &res))) {
		printk("%s: ERROR retrieving layout %d\n", __FUNCTION__, result);

		switch (result) {
		case -ENOENT:	/* NFS4ERR_BADLAYOUT */
			/* transient error, don't mark with NFS_INO_LAYOUT_FAILED */
			result = 1;
			break;

		case -EAGAIN:	/* NFS4ERR_LAYOUTTRYLATER, NFS4ERR_RECALLCONFLICT, NFS4ERR_LOCKED */
			nfsi->pnfs_layout_suspend = get_seconds() + 1;
			dprintk("%s: layout_get suspended until %ld\n",
			        __FUNCTION__, nfsi->pnfs_layout_suspend);
			/* FALLTHROUGH */
		case -EINVAL:	/* NFS4ERR_INVAL, NFSERR_BADIOMODE, NFS4ERR_UNKNOWN_LAYOUTTYPE */
		case -ENOTSUPP:	/* NFS4ERR_LAYOUTUNAVAILABLE */
		case -ETOOSMALL:/* NFS4ERR_TOOSMALL */
		default:
			/* mark with NFS_INO_LAYOUT_FAILED */
			break;
		}
		goto out;
	}

	if (res.layout.len <= 0) {
		printk("%s: ERROR!  Layout size is ZERO!\n",__FUNCTION__);
		result =  -EIO;
		goto out;
	}

	/* Inject layout blob into I/O device driver */
	layout_new = pnfs_inject_layout(nfsi,
					nfss->pnfs_curr_ld->ld_io_ops,
					res.layout.buf);
	if (layout_new == NULL) {
		printk("%s: ERROR!  Could not inject layout (%d)\n",__FUNCTION__,result);
		result =  -EIO;
		goto out;
	}

	if (res.return_on_close) {
		layout_new->roc_iomode = res.iomode;
		if (!layout_new->roc_iomode) {
			layout_new->roc_iomode = IOMODE_ANY;
		}
	}
	nfsi->current_layout = layout_new;

	result = 0;
out:

        /* remember that get layout failed and don't try again */
	if (result < 0)
		nfsi->pnfs_layout_state |= NFS_INO_LAYOUT_FAILED;

	/* res.layout.buf kalloc'ed by the xdr decoder? */
	if (res.layout.buf)
		kfree(res.layout.buf);
	dprintk("%s end (err:%d) state %d\n",
		__FUNCTION__,result,nfsi->pnfs_layout_state);
	return result;
}

/* Return true if a layout driver is being used for this mountpoint */
int
pnfs_enabled_sb(struct nfs_server* nfss)
{
	if (!nfss->pnfs_curr_ld)
		return 0;

	return 1;
}

/* Retrieve and return whether the layout driver wants I/O requests
 * to first travel through NFS I/O processing functions and the page
 * cache.  By default return 1;
 */
static int
use_page_cache(struct inode *inode)
{
	struct nfs_server* nfss = NFS_SERVER(inode);
	struct nfs_inode* nfsi = NFS_I(inode);
	int use_pagecache = 0;

	if (!pnfs_enabled_sb(nfss) ||
	    !nfss->pnfs_curr_ld->ld_policy_ops ||
	    !nfss->pnfs_curr_ld->ld_policy_ops->use_pagecache) {
		return 1;
	}

	use_pagecache = nfss->pnfs_curr_ld->ld_policy_ops->use_pagecache(nfsi->current_layout, inode);
	if (use_pagecache > 0)
		return 1;
	else
		return 0;
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
	struct nfs_server* nfss = NFS_SERVER(inode);
	struct nfs_inode* nfsi = NFS_I(inode);
	ssize_t threshold = -1;

	if (!pnfs_enabled_sb(nfss) ||
	    !nfss->pnfs_curr_ld->ld_policy_ops)
		return 0;

	if (iswrite && nfss->pnfs_curr_ld->ld_policy_ops->get_write_threshold) {
		threshold = nfss->pnfs_curr_ld->ld_policy_ops->get_write_threshold(nfsi->current_layout, inode);
		dprintk("%s wthresh: %Zd\n",__FUNCTION__, threshold);
		goto check;
	}

	if (!iswrite && nfss->pnfs_curr_ld->ld_policy_ops->get_read_threshold) {
		threshold = nfss->pnfs_curr_ld->ld_policy_ops->get_read_threshold(nfsi->current_layout, inode);
		dprintk("%s rthresh: %Zd\n",__FUNCTION__, threshold);
	}

check:
	if ((ssize_t)req_size <= threshold)
		return 1;
	else
		return 0;
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
	struct nfs_server* nfss = NFS_SERVER(inode);
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
	struct nfs_server* nfss = NFS_SERVER(inode);
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
	struct nfs_server* nfss = NFS_SERVER(inode);
	if (!pnfs_enabled_sb(nfss) ||
	    pnfs_get_type(inode) == LAYOUT_NFSV4_FILES ||
	    !pnfs_use_read(inode, count))
		return 1;

	return 0;
}

u32
pnfs_getboundary(struct inode* inode)
{
	struct pnfs_layout_type *laytype;
	struct layoutdriver_policy_operations *policy_ops;
	struct pnfs_layoutdriver_type *ld;

	laytype = NFS_I(inode)->current_layout;
	ld = NFS_SERVER(inode)->pnfs_curr_ld;
	if (!pnfs_enabled_sb(NFS_SERVER(inode)) || !laytype)
		return 0;
	policy_ops = ld->ld_policy_ops;

	/* The default is to not gather across stripes */
	if (policy_ops && policy_ops->gather_across_stripes) {
		if (policy_ops->gather_across_stripes(laytype->mountid))
			return 0;
	}
	if (policy_ops && policy_ops->get_stripesize) {
		return policy_ops->get_stripesize(laytype, inode);
	}

	return 0; /* Gather up to wsize/rsize */
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
	dprintk("%s: Begin (status %Zd)\n",__FUNCTION__, status);

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
int
pnfs_writepages(struct nfs_write_data* wdata, int how)
{
	struct nfs_writeargs *args = &wdata->args;
	struct inode *inode = wdata->inode;
	int numpages, status = -EIO, pgcount=0, temp;
	struct nfs_server* nfss = NFS_SERVER(inode);
	struct nfs_inode* nfsi = NFS_I(inode);

	dprintk("%s: Writing ino:%lu %u@%llu\n",
		__FUNCTION__,
		inode->i_ino,
		args->count,
		args->offset);

	/* Retrieve and set layout if not allready cached */
	if ((status = virtual_update_layout(inode,
					    args->context,
					    args->count,
					    args->offset,
					    IOMODE_RW))) {
		status = 1;	/* retry with nfs I/O */
		goto out;
	}

	if (!nfss->pnfs_curr_ld->ld_io_ops ||
	    !nfss->pnfs_curr_ld->ld_io_ops->write_pagelist) {
		printk("%s: ERROR, no layout driver write operation\n", __FUNCTION__);
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
		__FUNCTION__,
		how,
		numpages);
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
		dprintk("%s: LD write_pagelist returned status %d > 0\n",__FUNCTION__, status);
		pnfs_update_last_write(nfsi, args->offset, status);
		pnfs_need_layoutcommit(nfsi, wdata->args.context);
		status = 0;
	}

out:
	dprintk("%s: End Status %d\n",__FUNCTION__, status);
	return status;
}

/* Post-read completion function.  Invoked by non RPC layout drivers
 * to clean up read pages.
 */
static void
pnfs_read_done(struct nfs_read_data* data, ssize_t status, int eof)
{
	dprintk("%s: Begin (status %Zd)\n",__FUNCTION__, status);

	/* NFSv4 will have sunrpc call the callbacks */
	if (data->call_ops == NULL ||
	    pnfs_use_nfsv4_rproto(data->inode, data->args.count))
		return;

	/* Status is the number of bytes written or an error code */
	data->task.tk_status = status;
	data->res.eof = eof;
	data->res.count = status;
	pnfs_readpage_result_norpc(&data->task, data);
	nfs_readdata_release(data);
}

/*
 * Call the appropriate parallel I/O subsystem read function.
 * If no I/O device driver exists, or one does match the returned
 * fstype, then return a positive status for regular NFS processing.
 */
int
pnfs_readpages(struct nfs_read_data *rdata)
{
	struct nfs_readargs *args = &rdata->args;
	struct inode *inode = rdata->inode;
	int numpages, status = -EIO, pgcount=0, temp;
	struct nfs_server* nfss = NFS_SERVER(inode);
	struct nfs_inode* nfsi = NFS_I(inode);

	dprintk("%s: Reading ino:%lu %u@%llu\n",
		__FUNCTION__,
		inode->i_ino,
		args->count,
		args->offset);

	/* Retrieve and set layout if not allready cached */
	if ((status = virtual_update_layout(inode,
					    args->context,
					    args->count,
					    args->offset,
					    IOMODE_RW)))
	{
		printk(KERN_WARNING "%s: ERROR %d from virtual_update_layout\n",
			__FUNCTION__, status);
		status = 1;
		goto out;
	}
	if (!nfss->pnfs_curr_ld->ld_io_ops ||
	    !nfss->pnfs_curr_ld->ld_io_ops->read_pagelist) {
		printk("%s: ERROR, no layout driver read operation\n", __FUNCTION__);
		status = 1;
		goto out;
	}

        /* Determine number of pages.
	 */
	pgcount = args->pgbase + args->count;
	temp = pgcount % PAGE_CACHE_SIZE;
	numpages = pgcount / PAGE_CACHE_SIZE;
	if (temp != 0)
		numpages++;

	dprintk("%s: Calling layout driver read with %d pages\n",__FUNCTION__, numpages);
	status = nfss->pnfs_curr_ld->ld_io_ops->read_pagelist(nfsi->current_layout,
							      inode,
							      args->pages,
							      args->pgbase,
							      numpages,
							      (loff_t)args->offset,
							      args->count,
							      rdata);
	if (status > 0) {
		dprintk("%s: LD read_pagelist returned status %d > 0\n",__FUNCTION__, status);
		status = 0;
	}

 out:
	dprintk("%s: End Status %d\n",__FUNCTION__, status);
	return status;
}

int pnfs_try_to_read_data(struct nfs_read_data *data,
                        const struct rpc_call_ops *call_ops)
{
	int status;

	dprintk("%s:Begin\n",__FUNCTION__);
	/* Only create an rpc request if utilizing NFSv4 I/O */
	if (!pnfs_use_read(data->inode, data->args.count)) {
		dprintk("%s:End not using pnfs\n",__FUNCTION__);
		return 1;
	} else {
		dprintk("%s Utilizing pNFS I/O\n",__FUNCTION__);
		data->call_ops = call_ops;
		data->pnfsflags |= PNFS_USE_DS;
		if((status = pnfs_readpages(data)))
			return status;
		return 0;
	}
}

/*
 * Call the appropriate parallel I/O subsystem read function.
 * If no I/O device driver exists, or one does match the returned
 * fstype, then call regular NFS processing.
 */
ssize_t
pnfs_file_read(struct file* filp,
	       char __user *buf,
	       size_t count,
	       loff_t* pos)
{
	struct dentry * dentry = filp->f_dentry;
	struct inode* inode = dentry->d_inode;
	ssize_t result = count;
	struct nfs_inode* nfsi = NFS_I(inode);
	struct nfs_server* nfss = NFS_SERVER(inode);

	dfprintk(IO, "%s:(%s/%s, %lu@%lu)\n",
		 __FUNCTION__,
		 dentry->d_parent->d_name.name,
		 dentry->d_name.name,
		 (unsigned long) count,
		 (unsigned long) *pos);

	/* Using NFS page cache with pNFS */
	if (use_page_cache(inode))
		goto fallback;

	/* Small I/O Optimization */
	if (below_threshold(inode, count, 0)) {
		dfprintk(IO, "%s: Below Read threshold, using NFSv4 read\n",__FUNCTION__);
		goto fallback;
	}

	/* Step 1: Retrieve and set layout if not allready cached*/
	if ((result = virtual_update_layout(inode,
					    (struct nfs_open_context *)filp->private_data,
					    count,
					    *pos,
					    IOMODE_READ))) {
		dfprintk(IO, "%s: Could not get layout result=%Zd, using NFSv4 read\n",__FUNCTION__, result);
		goto fallback;
	}

	/* Step 2: Call I/O device driver's read function */
	if (!nfss->pnfs_curr_ld->ld_io_ops &&
	    nfss->pnfs_curr_ld->ld_io_ops->read) {
		dfprintk(IO, "%s: No LD read function, using NFSv4 read\n",__FUNCTION__);
		goto fallback;
	}

	result = nfss->pnfs_curr_ld->ld_io_ops->read(nfsi->current_layout,
						     filp, buf, count, pos);
	dprintk("%s end (err:%Zd)\n",__FUNCTION__,result);
	return result;

fallback:
	return do_sync_read(filp, buf, count, pos);
}

int pnfs_try_to_write_data(struct nfs_write_data *data,
				const struct rpc_call_ops *call_ops, int how)
{
	int status;

	dprintk("%s:Begin\n",__FUNCTION__);
	/* Only create an rpc request if utilizing NFSv4 I/O */
	if (!pnfs_use_write(data->inode, data->args.count)) {
		dprintk("%s:End. not using pnfs\n",__FUNCTION__);
		return 1;
	} else {
		dprintk("%s Utilizing pNFS I/O\n",__FUNCTION__);
		data->call_ops = call_ops;
		data->pnfsflags |= PNFS_USE_DS;
		data->how = how; /* XXX do we really need this? */
                if((status = pnfs_writepages(data, how)))
			return status;
		return 0;
	}
}

/*
 * Call the appropriate parallel I/O subsystem write function.
 * If no I/O device driver exists, or one does match the returned
 * fstype, then call regular NFS processing.
 */
ssize_t
pnfs_file_write(struct file* filp,
		const char __user *buf,
		size_t count,
		loff_t* pos)
{
	struct dentry * dentry = filp->f_dentry;
	struct inode* inode = dentry->d_inode;
	ssize_t result = count;
	loff_t pos_orig = *pos;
	const int isblk = S_ISBLK(inode->i_mode);
	struct nfs_server* nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);

	dfprintk(IO, "%s:(%s/%s(%ld), %lu@%lu)\n",
		 __FUNCTION__,
		 dentry->d_parent->d_name.name,
		 dentry->d_name.name,
		 inode->i_ino,
		 (unsigned long) count,
		 (unsigned long) *pos);

	/* Using NFS page cache with pNFS */
	if (use_page_cache(inode))
		goto fallback;

	/* Small I/O Optimization */
	if (below_threshold(inode, count, 1)) {
		dfprintk(IO, "%s: Below write threshold, using NFSv4 write\n",__FUNCTION__);
		goto fallback;
	}

	/* Need to adjust write param if this is an append, etc */
	generic_write_checks(filp,pos,&count,isblk);

	dprintk("%s:Readjusted %lu@%lu)\n",__FUNCTION__,
		(unsigned long) count, (unsigned long) *pos);

	/* Step 1: Retrieve and set layout if not allready cached*/
	if ((result = virtual_update_layout(inode,
					    (struct nfs_open_context *)filp->private_data,
					    count,
					    *pos,
					    IOMODE_RW))) {
		dfprintk(IO, "%s: Could not get layout result=%Zd, using NFSv4 write\n",__FUNCTION__, result);
		goto fallback;
	}

	/* Step 2: Call I/O device driver's write function */
	if (!nfss->pnfs_curr_ld->ld_io_ops &&
	    nfss->pnfs_curr_ld->ld_io_ops->write) {
		dfprintk(IO, "%s: No LD write function, using NFSv4 write\n",__FUNCTION__);
		goto fallback;
	}

	result = nfss->pnfs_curr_ld->ld_io_ops->write(nfsi->current_layout,
						      filp, buf, count, pos);

	/* Update layoutcommit info.
	 * TODO: This assumes the layout driver wrote synchronously.
	 * This is fine for PVFS2, the only current layout driver to
	 * use the read/write interface. */
	if (result > 0) {
		pnfs_update_last_write(nfsi, pos_orig, result);
		pnfs_need_layoutcommit(nfsi, (struct nfs_open_context *)filp->private_data);
	}
	dprintk("%s end (err:%Zd)\n",__FUNCTION__,result);
	return result;

fallback:
	return do_sync_write(filp, buf, count, pos);
}

int pnfs_try_to_commit(struct nfs_write_data *data, struct list_head *head, int how)
{
	int status;

	dprintk("%s:Begin\n",__FUNCTION__);
	if (!pnfs_use_write(data->inode, -1)) {
		dprintk("%s:End not using pnfs\n",__FUNCTION__);
		return 1;
	} else {
		/* data->call_ops already set in nfs_commit_rpcsetup */
		dprintk("%s Utilizing pNFS I/O\n",__FUNCTION__);
		if((status = pnfs_commit(data->inode, head, how, data)) < 0)
			return status;
		return 0;
	}
}

/* pNFS Commit callback function for non-file layout drivers */
static void
pnfs_commit_done(struct nfs_write_data *data, ssize_t status)
{
	dprintk("%s: Begin (status %Zd)\n",__FUNCTION__, status);

	/* NFSv4 will have sunrpc call the callbacks */
	if (pnfs_use_nfsv4_wproto(data->inode, -1))
		return;

	/* Status is the number of bytes written or an error code */
	data->task.tk_status = status;
	pnfs_commit_done_norpc(&data->task, data);
	data->call_ops->rpc_release(data);
}

int
pnfs_commit(struct inode* inode,
	    struct list_head *head,
	    int sync,
	    struct nfs_write_data *data)
{
	int result = 0;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_server* nfss = NFS_SERVER(inode);
	dprintk("%s: Begin\n",__FUNCTION__);

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
		nfs_commit_rpcsetup(data, sync);
		nfs_execute_write(data);
		return 0;
	}

	dprintk("%s: Calling layout driver commit\n",__FUNCTION__);
	result = nfss->pnfs_curr_ld->ld_io_ops->commit(nfsi->current_layout,
						       inode, head, sync, data);

	dprintk("%s end (err:%d)\n",__FUNCTION__,result);
	return result;
}

int
pnfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int result = 0;
	struct inode *inode = dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_server* nfss = NFS_SERVER(inode);
	dprintk("%s: Begin\n",__FUNCTION__);

	/* pNFS is only for v4
	 * Only fsync nfs if an outstanding nfs request requires it
	 * Some problems seem to be happening if ncommit and ndirty
	 * are both 0 and I still don't call nfs_fsync
	 */
	if (use_page_cache(inode)) {
		dfprintk(IO, "%s: Calling nfs_fsync\n",__FUNCTION__);
		result = nfs_fsync(file,dentry,datasync);
		goto out;
	}

	if (!nfss->pnfs_curr_ld->ld_io_ops->fsync) {
		dprintk("%s: Layoutdriver lacks fsync function!\n",__FUNCTION__);
		result = -EIO;
		goto out;
	}

	/* Retrieve and set layout if not allready cached.
	 * This is necessary since read/write may not have necessarily
	 * been already called.  Just put in any random count and offset.
	 * TODO: May need special count and offset depending on how file system
	 * work that actually pay attention to such values.
	 */
	if ((result = virtual_update_layout(inode,
					    (struct nfs_open_context *)file->private_data,
					    0,
					    0,
					    IOMODE_RW))) {
		result = -EIO;
		goto out;
	}

	dprintk("%s: Calling layout driver fsync\n",__FUNCTION__);
	result = nfss->pnfs_curr_ld->ld_io_ops->fsync(nfsi->current_layout,
						      file,
						      dentry,
						      datasync);

out:
	dprintk("%s end (err:%d)\n",__FUNCTION__,result);
	return result;
}

int
pnfs_getdevicelist(struct super_block *sb, struct pnfs_devicelist* devlist)
{
	struct nfs_server *server = NFS_SB(sb);
	return nfs4_pnfs_getdevicelist(server, devlist);
}

/* Retrieve the device information for a device.
 */
int
pnfs_getdeviceinfo(struct super_block *sb, u32 dev_id, struct pnfs_device* dev)
{
	struct nfs_server *server = NFS_SB(sb);
	int rc;

	rc = nfs4_pnfs_getdeviceinfo(server, dev_id, dev);

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

	dprintk("%s: (status %d)\n", __FUNCTION__, status);

	/* TODO: For now, set an error in the open context (just like
	 * if a commit failed) We may want to do more, much more, like
	 * replay all writes through the NFSv4
	 * server, or something.
	 */
	if (status < 0) {
		printk("%s, Layoutcommit Failed! = %d\n", __FUNCTION__, status);
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

/* Execute a layoutcommit to the server */
static void
pnfs_execute_layoutcommit(struct pnfs_layoutcommit_data *data)
{
	struct rpc_clnt *clnt = NFS_CLIENT(data->inode);
	sigset_t oldset;
	rpc_clnt_sigmask(clnt, &oldset);
	lock_kernel();
	rpc_execute(&data->task);
	unlock_kernel();
	rpc_clnt_sigunmask(clnt, &oldset);
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

/*
 * Set up the argument/result storage required for the RPC call.
 */
static int
pnfs_layoutcommit_setup(struct pnfs_layoutcommit_data *data, int sync)
{
	struct nfs_inode *nfsi = NFS_I(data->inode);
	struct nfs_server *nfss = NFS_SERVER(data->inode);
	int result = 0;

	dprintk("%s Begin (sync:%d)\n", __FUNCTION__, sync);
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
	data->args.offset = nfsi->pnfs_write_begin_pos;
	data->args.length = nfsi->pnfs_write_end_pos - nfsi->pnfs_write_begin_pos + 1;
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

	/* Finalize the task. */
	rpc_init_task(&data->task, NFS_CLIENT(data->inode), RPC_TASK_ASYNC,
		      &pnfs_layoutcommit_ops, data);

	NFS_PROTO(data->inode)->pnfs_layoutcommit_setup(data);

	data->task.tk_priority = RPC_PRIORITY_NORMAL;
	data->task.tk_cookie = (unsigned long)data->inode;

	dprintk("NFS: %4d initiated layoutcommit call. %llu@%llu lbw: %llu "
		"type: %d new_layout_size: %d\n",
		data->task.tk_pid,
		data->args.length,
		data->args.offset,
		data->args.lastbytewritten,
		data->args.layout_type,
		data->args.new_layout_size);
out:
	dprintk("%s End Status %d\n", __FUNCTION__, result);
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

	dprintk("%s Begin (sync:%d)\n", __FUNCTION__, sync);

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
	dprintk("%s end (err:%d)\n", __FUNCTION__, status);
	return status;
out_unlock:
	spin_unlock(&pnfs_spinlock);
	goto out;
}

/* Callback operations for layout drivers.
 */
struct pnfs_client_operations pnfs_ops = {
        .nfs_fsync = nfs_fsync,
	.nfs_getdevicelist = pnfs_getdevicelist,
	.nfs_getdeviceinfo = pnfs_getdeviceinfo,
	.nfs_readlist_complete = pnfs_read_done,
	.nfs_writelist_complete = pnfs_writeback_done,
	.nfs_commit_complete = pnfs_commit_done,
};

int
pnfs_rsize(struct inode *inode, unsigned int count, struct nfs_read_data *rdata)
{
	if (count >= 0 && below_threshold(inode, count, 0))
		return NFS_SERVER(inode)->rsize;

	rdata->pnfsflags |= PNFS_USE_DS;
	return NFS_SERVER(inode)->ds_rsize;
}

int
pnfs_wsize(struct inode *inode, unsigned int count, struct nfs_write_data *wdata)
{
	if (count >= 0 && below_threshold(inode, count, 1))
		return NFS_SERVER(inode)->wsize;

	wdata->pnfsflags |= PNFS_USE_DS;
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
