/*
 *  linux/fs/nfs/nfs4filelayout.c
 *
 *  Module for the pnfs nfs4 file layout driver.
 *  Defines all I/O and Policy interface operations, plus code
 *  to register itself with the pNFS client.
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

#ifdef CONFIG_PNFS

#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/pnfs_xdr.h>
#include <linux/nfs4_pnfs.h>

#include "nfs4filelayout.h"
#include "nfs4_fs.h"

#define NFSDBG_FACILITY         NFSDBG_FILELAYOUT

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dean Hildebrand <dhildebz@eecs.umich.edu>");
MODULE_DESCRIPTION("The NFSv4 file layout driver");

extern void nfs_execute_read(struct nfs_read_data *data);
extern void nfs_readdata_release(void *data);
extern int nfs_flush_task_priority(int how);
extern void nfs_writedata_release(void *data);
extern void nfs_execute_write(struct nfs_write_data *data);
extern void nfs_commit_rpcsetup(struct nfs_write_data *data, int sync);
extern struct nfs_write_data *nfs_commit_alloc(void);
extern void nfs_commit_free(struct nfs_write_data *p);
extern void nfs_initiate_write(struct nfs_write_data *, struct rpc_clnt *, const struct rpc_call_ops *, int);
extern void nfs_initiate_read(struct nfs_read_data *data, struct rpc_clnt *clnt, const struct rpc_call_ops *call_ops);
extern void nfs_read_validate(struct rpc_task *task, void *calldata);
extern void nfs_readdata_release(void *data);
extern void nfs_write_validate(struct rpc_task *task, void *calldata);

/* Callback operations to the pNFS client */
struct pnfs_client_operations *pnfs_callback_ops;

/* Initialize a mountpoint by retrieving the list of
 * available devices for it.
 * Return the pnfs_mount_type structure so the
 * pNFS_client can refer to the mount point later on
 */
struct pnfs_mount_type*
filelayout_initialize_mountpoint(struct super_block *sb, struct nfs_fh *fh)
{
	struct filelayout_mount_type *fl_mt;
	struct pnfs_mount_type *mt;
	struct pnfs_devicelist *dlist;
	int status;

	dlist = kmalloc(sizeof(struct pnfs_devicelist), GFP_KERNEL);
	if (!dlist)
		goto error_ret;

	fl_mt = kmalloc(sizeof(struct filelayout_mount_type), GFP_KERNEL);
	if (!fl_mt)
		goto cleanup_dlist;

	/* Initialize nfs4 file layout specific device list structure */
	fl_mt->hlist = kmalloc(sizeof(struct nfs4_pnfs_dev_hlist), GFP_KERNEL);
	if (!fl_mt->hlist)
		goto cleanup_fl_mt;

	mt = kmalloc(sizeof(struct pnfs_mount_type), GFP_KERNEL);
	if (!mt)
		goto cleanup_fl_mt;

	fl_mt->fl_sb = sb;
	mt->mountid = (void *)fl_mt;

	/* Retrieve device list from server */
	status = pnfs_callback_ops->nfs_getdevicelist(sb, fh, dlist);
	if (status)
		goto cleanup_mt;

	status = nfs4_pnfs_devlist_init(fl_mt->hlist);
	if (status)
		goto cleanup_mt;

	/*
	 * Decode opaque devicelist and add to list of available
	 * devices (data servers
	 */
	status = decode_and_add_devicelist(fl_mt, dlist);
	if (status)
		goto cleanup_mt;

	kfree(dlist);
	dprintk("%s device list has been initialized successfully\n",
		__FUNCTION__);
	return mt;

cleanup_mt: ;
	kfree(mt);

cleanup_fl_mt: ;
	kfree(fl_mt->hlist);
	kfree(fl_mt);

cleanup_dlist: ;
	kfree(dlist);

error_ret: ;
	printk(KERN_WARNING "%s device list could not be initialized\n",
				__FUNCTION__);

	return NULL;
}

/* Uninitialize a mountpoint by destroying its device list.
 */
int
filelayout_uninitialize_mountpoint(struct pnfs_mount_type *mountid)
{
	struct filelayout_mount_type *fl_mt = NULL;

	if (mountid)
		fl_mt = (struct filelayout_mount_type *)mountid->mountid;

	nfs4_pnfs_devlist_destroy(fl_mt->hlist);

	if (fl_mt != NULL)
		kfree(fl_mt);
	kfree(mountid);

	return 0;
}

extern struct rpc_call_ops nfs_read_partial_ops;

/* This function is used by the layout driver to calculate the
 * offset of the file on the dserver based on whether the
 * layout type is STRIPE_DENSE or STRIPE_SPARSE
 */
loff_t
filelayout_get_dserver_offset(loff_t offset, struct nfs4_filelayout *layout)
{
	if (layout == NULL)
		return offset;

	switch (layout->stripe_type) {
	case STRIPE_SPARSE:
		return offset;

	case STRIPE_DENSE:
	{
		u32 stripe_size;
		u32 stripe_unit;
		loff_t off;
		loff_t tmp;
		u32 stripe_unit_idx;

		stripe_size = layout->stripe_unit * layout->num_fh;
		/* XXX I do this because do_div seems to take a 32 bit dividend */
		stripe_unit = layout->stripe_unit;
		tmp = off = offset;

		do_div(off, stripe_size);
		stripe_unit_idx = do_div(tmp, stripe_unit);

		return off * stripe_unit + stripe_unit_idx;
	}

	default:
		BUG();
	}

	/* We should never get here... just to stop the gcc warning */
	return 0;
}

/* Call ops for the async read/write cases
 * In the case of dense layouts, the offset needs to be reset to its
 * original value.
 */
static void filelayout_read_call_done(struct rpc_task *task, void *data)
{
	struct nfs_read_data *rdata = (struct nfs_read_data *)data;

	if (rdata->orig_offset)
		rdata->args.offset = rdata->orig_offset;

	/* Call the NFS call ops now */
	rdata->call_ops->rpc_call_done(task, data);
}

static void filelayout_write_call_done(struct rpc_task *task, void *data)
{
	struct nfs_write_data *wdata = (struct nfs_write_data *)data;

	if (wdata->orig_offset)
		wdata->args.offset = wdata->orig_offset;

	/* Call the NFS call ops now */
	wdata->call_ops->rpc_call_done(task, data);
}

struct rpc_call_ops filelayout_read_call_ops = {
	.rpc_call_validate_args = nfs_read_validate,
	.rpc_call_done = filelayout_read_call_done,
	.rpc_release = nfs_readdata_release,
};

struct rpc_call_ops filelayout_write_call_ops = {
	.rpc_call_validate_args = nfs_write_validate,
	.rpc_call_done = filelayout_write_call_done,
	.rpc_release = nfs_writedata_release,
};

/* Perform sync or async reads.
 *
 * An optimization for the NFS file layout driver
 * allows the original read/write data structs to be passed in the
 * last argument.
 *

 * This is called after the pNFS client has already created, so I pass it
 * in via the last argument (void*).  I think this is the only way as there
 * are just too many NFS specific arguments in the read/write data structs
 * to pass to the layout drivers.
 *
 * TODO:
 * 1. This is a lot of arguments, create special non-nfs-specific structure?
 */
ssize_t filelayout_read_pagelist(
	struct pnfs_layout_type *layoutid,
	struct inode *inode,
	struct page **pages,
	unsigned int pgbase,
	unsigned nr_pages,
	loff_t offset,
	size_t count,
	struct nfs_read_data *data)
{
	struct nfs4_filelayout *nfslay = NULL;
	struct nfs4_pnfs_dserver dserver;
	int status;

	if (layoutid) {
		nfslay = (struct nfs4_filelayout *)layoutid->layoutid;
		/* Retrieve the correct rpc_client for the byte range */
		status = nfs4_pnfs_dserver_get(inode,
						nfslay,
						offset,
						count,
						&dserver);
		if (status) {
			printk(KERN_ERR "%s: dserver get failed status %d use MDS\n",
							__FUNCTION__, status);
			data->pnfs_client = NFS_CLIENT(inode);
			data->ds_nfs_client = NULL;
			data->args.fh = NFS_FH(inode);
			status = 0;
		} else {
			struct nfs4_pnfs_ds *ds = dserver.dev->ds_list[0];

			/* just try the first data server for the index..*/
			data->pnfs_client = ds->ds_clp->cl_rpcclient;
			data->ds_nfs_client = ds->ds_clp;
			data->args.fh = dserver.fh;

			/* Now get the file offset on the dserver
			 * Set the read offset to this offset, and
			 * save the original offset in orig_offset
			 */
			data->args.offset = filelayout_get_dserver_offset(offset, nfslay);
			data->orig_offset = offset;
		}
	} else { /* If no layout use MDS */
		dprintk("%s: no layout, use MDS\n", __FUNCTION__);
		data->pnfs_client = NFS_CLIENT(inode);
		data->ds_nfs_client = NULL;
		data->args.fh = NFS_FH(inode);
	}

	/* Perform an asynchronous read */
	/* Now get the file offset on the dserver
	 * Set the write offset to this offset, and
	 * save the original offset in orig_offset
	 */
	data->args.offset = filelayout_get_dserver_offset(offset, nfslay);
	data->orig_offset = offset;

	BUG_ON(data->pnfsflags & PNFS_ISSYNC);
	nfs_initiate_read(data, data->pnfs_client, &filelayout_read_call_ops);

	/* In the case of aync reads, the offset will be reset in the
	 * call_ops->rpc_call_done() routine.
	 *
	 * In the case of aync writes, the offset will be reset in the
	 * call_ops->rpc_call_done() routine
	 */
	status = 0;

	return status;
}

/* Perform sync or async writes.
 *
 * TODO: See filelayout_read_pagelist.
 */
ssize_t filelayout_write_pagelist(
	struct pnfs_layout_type *layoutid,
	struct inode *inode,
	struct page **pages,
	unsigned int pgbase,
	unsigned nr_pages,
	loff_t offset,
	size_t count,
	int sync,
	struct nfs_write_data *data)
{
	struct nfs4_filelayout *nfslay = (struct nfs4_filelayout *)layoutid->layoutid;
	struct nfs4_pnfs_dserver dserver;
	struct nfs4_pnfs_ds *ds;
	struct nfs_page *req;
	struct list_head *h;
	int status;

	/* Retrieve the correct rpc_client for the byte range */
	status = nfs4_pnfs_dserver_get(inode,
					nfslay,
					offset,
					count,
					&dserver);
	if (status) {
		dprintk("%s failed to get dataserver\n",
						__FUNCTION__);
		data->ds_nfs_client = NULL;
		return -EIO;
	} else {
		/* just try the first data server for the index.. */
		ds = dserver.dev->ds_list[0];
		data->pnfs_client = ds->ds_clp->cl_rpcclient;
		data->ds_nfs_client = ds->ds_clp;
		data->args.fh = dserver.fh;
	}
	dprintk("%s set wb_devip: wb_devport %x:%hu\n", __FUNCTION__,
			htonl(ds->ds_ip_addr), ntohs(ds->ds_port));

	list_for_each(h, &data->pages) {
		req = list_entry(h, struct nfs_page, wb_list);
		req->wb_devip = ds->ds_ip_addr;
		req->wb_devport = ds->ds_port;
	}

	/* Now get the file offset on the dserver
	 * Set the write offset to this offset, and
	 * save the original offset in orig_offset
	 * the offset will be reset in the call_ops->rpc_call_done() routine.
	 */
	data->args.offset = filelayout_get_dserver_offset(offset, nfslay);
	data->orig_offset = offset;

	/* Perform an asynchronous write */
	BUG_ON(data->pnfsflags & PNFS_ISSYNC);
	nfs_initiate_write(data, data->pnfs_client,
			&filelayout_write_call_ops, sync);

	return 0;
}

/* Create a filelayout layout structure and return it.  The pNFS client
 * will use the pnfs_layout_type type to refer to the layout for this
 * inode from now on.
 */
struct pnfs_layout_type*
filelayout_alloc_layout(struct pnfs_mount_type *mountid, struct inode *inode)
{
	struct pnfs_layout_type *pnfslay = NULL;
	struct nfs4_filelayout *nfslay = NULL;

	dprintk("NFS_FILELAYOUT: allocating layout\n");

	pnfslay = kzalloc(sizeof(struct pnfs_layout_type), GFP_KERNEL);
	if (!pnfslay)
		return NULL;
	nfslay = kzalloc(sizeof(struct nfs4_filelayout), GFP_KERNEL);
	if (!nfslay)
		return NULL;

	pnfslay->layoutid = (void *)nfslay;
	pnfslay->mountid = mountid;
	return pnfslay;
}

/* Free a filelayout layout structure
 */
void
filelayout_free_layout(struct pnfs_layout_type **layoutidp,
		       struct inode *inode,
		       struct nfs4_pnfs_layout_segment *range)
{
	struct nfs4_filelayout *nfslay = NULL;
	struct pnfs_layout_type *layoutid;

	dprintk("NFS_FILELAYOUT: freeing layout\n");

	layoutid = *layoutidp;
	if (layoutid)
		nfslay = (struct nfs4_filelayout *)layoutid->layoutid;
	if (nfslay != NULL)
		kfree(nfslay);
	kfree(layoutid);
	*layoutidp = NULL;
}

/* Decode layout and store in layoutid.  Overwrite any existing layout
 * information for this file.
 */
struct pnfs_layout_type*
filelayout_set_layout(struct pnfs_layout_type *layoutid, struct inode *inode,
			struct nfs4_pnfs_layoutget_res *lgr)
{
	struct nfs4_filelayout *fl = NULL;
	int i;
	uint32_t *p = (uint32_t *)lgr->layout.buf;
	uint32_t nfl_util;

	dprintk("%s set_layout_map Begin\n", __FUNCTION__);

	if (!layoutid)
		goto nfserr;
	fl = (struct nfs4_filelayout *)layoutid->layoutid;
	if (!fl)
		goto nfserr;

	READ32(fl->dev_id);
	READ32(nfl_util);
	if (nfl_util & NFL4_UFLG_COMMIT_THRU_MDS)
		fl->commit_through_mds = 1;
	if (nfl_util & NFL4_UFLG_DENSE)
		fl->stripe_type = STRIPE_DENSE;
	else
		fl->stripe_type = STRIPE_SPARSE;
	fl->stripe_unit = nfl_util & ~NFL4_UFLG_MASK;

	READ32(fl->first_stripe_index);
	READ32(fl->num_fh);

	dprintk("DEBUG: %s: dev_id %u nfl_util 0x%X num_fh %u\n", __func__,
				fl->dev_id, nfl_util, fl->num_fh);

	for (i = 0; i < fl->num_fh; i++) {
		/* fh */
		memset(&fl->fh_array[i], 0, sizeof(struct nfs_fh));
		READ32(fl->fh_array[i].size);
		COPYMEM(fl->fh_array[i].data, fl->fh_array[i].size);
		dprintk("DEBUG: %s: fh len %d\n", __func__,
					fl->fh_array[i].size);
	}

	return layoutid;
nfserr:
	return NULL;
}

/* Call nfs fsync function to flush buffers and eventually call
 * the filelayout_write_pagelist and filelayout_commit functions.
 */
int
filelayout_fsync(struct pnfs_layout_type *layoutid,
		struct file *file,
		struct dentry *dentry,
		int datasync)
{
	return pnfs_callback_ops->nfs_fsync(file, dentry, datasync);
}

/* TODO: Technically we would need to execute a COMMIT op to each
 * data server on which a page in 'pages' exists.
 * Once we fix this, we will need to invoke the pnfs_commit_complete callback.
 */
int
filelayout_commit(struct pnfs_layout_type *layoutid, struct inode *ino, struct list_head *pages, int sync, struct nfs_write_data *data)
{
	struct nfs_write_data   *dsdata = NULL;
	struct pnfs_layout_type *laytype;
	struct nfs4_filelayout *nfslay;
	struct nfs4_pnfs_dev_item *dev;
	struct nfs4_pnfs_dev *fdev;
	struct nfs4_pnfs_dserver dserver;
	struct nfs4_pnfs_ds *ds;
	struct nfs_page *first;
	struct nfs_page *req;
	struct list_head *pos, *tmp;
	int i;

	laytype = NFS_I(ino)->current_layout;
	nfslay = (struct nfs4_filelayout *)layoutid->layoutid;

	dprintk("%s data %p pnfs_client %p nfslay %p\n",
			__FUNCTION__, data, data->pnfs_client, nfslay);

	if (nfslay->commit_through_mds) {
		dprintk("%s data %p commit through mds\n", __FUNCTION__, data);
		nfs_commit_rpcsetup(data, sync);
		nfs_execute_write(data);
		return 0;
	}
	dev = nfs4_pnfs_device_item_get(ino, nfslay->dev_id);
	fdev = &dev->stripe_devs[0];

	for (i = 0; i < nfslay->num_fh; i++) {
		/* just try the first data server for the index..*/
		ds = fdev->ds_list[0];

		if (!dsdata) {
			unsigned int pgcnt = 0;

			list_for_each_safe(pos, tmp, &data->pages) {
				req = nfs_list_entry(pos);
				if (req->wb_devip == ds->ds_ip_addr &&
				    req->wb_devport == ds->ds_port)
					pgcnt++;
			}
			dsdata = nfs_commit_alloc();
		}
		if (!dsdata)
			goto out_bad;
		list_for_each_safe(pos, tmp, &data->pages) {
			req = nfs_list_entry(pos);
			if (req->wb_devip == ds->ds_ip_addr &&
			    req->wb_devport == ds->ds_port) {
				nfs_list_remove_request(req);
				nfs_list_add_request(req, &dsdata->pages);
			}
		}
		if (list_empty(&dsdata->pages)) {
			if (list_empty(&data->pages)) {
				dprintk("%s exit i %d devid %d\n",
					__FUNCTION__, i, nfslay->dev_id);
				nfs_commit_free(dsdata);
				return 0;
			} else
				continue;
		}
		first = nfs_list_entry(dsdata->pages.next);

		dprintk("%s call nfs_commit_rpcsetup i %d devid %d\n",
			__FUNCTION__, i, nfslay->dev_id);

		dsdata->pnfs_client = ds->ds_clp->cl_rpcclient;
		dsdata->ds_nfs_client =  ds->ds_clp;
		dserver.dev = fdev;

		nfs_commit_rpcsetup(dsdata, sync);

		/* TODO: Is the FH different from NFS_FH(data->inode)?
		 * (set in nfs_commit_rpcsetup)
		 */
		dserver.fh = &nfslay->fh_array[i];
		dsdata->args.fh = dserver.fh;

		nfs_execute_write(dsdata);
		dsdata = NULL;
		fdev++;
	}

	/* Release original commit data since it is not used */
	nfs_commit_free(data);
	return 0;

out_bad:
	nfs_commit_free(data);
	return -ENOMEM;
}

/* Return the stripesize for the specified file.
 */
ssize_t
filelayout_get_stripesize(struct pnfs_layout_type *layoutid, struct inode *inode)
{
	struct nfs4_filelayout *fl = (struct nfs4_filelayout *)layoutid->layoutid;
	ssize_t stripesize = fl->stripe_unit;
	return stripesize;
}

/* Split wsize/rsize chunks so they do not span multiple data servers
 */
int
filelayout_gather_across_stripes(struct pnfs_mount_type *mountid)
{
	return 0;
}

/*
 * filelayout_pg_test(). Called by nfs_can_coalesce_requests()
 *
 * For writes which come from the flush daemon, set the bsize on the fly.
 * reads set the bsize in pnfs_pageio_init_read.
 *
 * return 1 :  coalesce page
 * return 0 :  don't coalesce page
 */
int
filelayout_pg_test(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
		   struct nfs_page *req)
{
	u32 p_stripe, r_stripe;

	if (!pgio->pg_iswrite)
		goto boundary;

	if (pgio->pg_bsize != NFS_SERVER(pgio->pg_inode)->ds_wsize &&
	    pgio->pg_count > pgio->pg_threshold)
		pgio->pg_bsize = NFS_SERVER(pgio->pg_inode)->ds_wsize;

boundary:
	if (pgio->pg_boundary == 0)
		return 1;
	p_stripe = prev->wb_index << PAGE_CACHE_SHIFT;
	do_div(p_stripe, pgio->pg_boundary);
	r_stripe = req->wb_index << PAGE_CACHE_SHIFT;
	do_div(r_stripe, pgio->pg_boundary);

	return (p_stripe == r_stripe);
}

/* Use the NFSv4 page cache
*/
int
filelayout_use_pagecache(struct pnfs_layout_type *layoutid, struct inode *inode)
{
	return 1;
}

/* Issue a layoutget in the same compound as OPEN
 */
int
filelayout_layoutget_on_open(struct pnfs_mount_type *mountid)
{
	return 1;
}

ssize_t
filelayout_get_io_threshold(struct pnfs_layout_type *layoutid, struct inode *inode)
{
	return -1;
}

struct layoutdriver_io_operations filelayout_io_operations = {
	.fsync                   = filelayout_fsync,
	.commit                  = filelayout_commit,
	.read_pagelist           = filelayout_read_pagelist,
	.write_pagelist          = filelayout_write_pagelist,
	.set_layout              = filelayout_set_layout,
	.alloc_layout            = filelayout_alloc_layout,
	.free_layout             = filelayout_free_layout,
	.initialize_mountpoint   = filelayout_initialize_mountpoint,
	.uninitialize_mountpoint = filelayout_uninitialize_mountpoint,
};

struct layoutdriver_policy_operations filelayout_policy_operations = {
	.get_stripesize        = filelayout_get_stripesize,
	.gather_across_stripes = filelayout_gather_across_stripes,
	.pg_test               = filelayout_pg_test,
	.use_pagecache         = filelayout_use_pagecache,
	.layoutget_on_open     = filelayout_layoutget_on_open,
	.get_read_threshold    = filelayout_get_io_threshold,
	.get_write_threshold   = filelayout_get_io_threshold,
};

struct pnfs_layoutdriver_type filelayout_type = {
	.id = LAYOUT_NFSV4_FILES,
	.name = "LAYOUT_NFSV4_FILES",
	.ld_io_ops = &filelayout_io_operations,
	.ld_policy_ops = &filelayout_policy_operations,
};

static int __init nfs4filelayout_init(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Registering...\n",
	       __FUNCTION__);

	/* Need to register file_operations struct with global list to indicate
	* that NFS4 file layout is a possible pNFS I/O module
	*/
	pnfs_callback_ops = pnfs_register_layoutdriver(&filelayout_type);

	return 0;
}

static void __exit nfs4filelayout_exit(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Unregistering...\n",
	       __FUNCTION__);

	/* Unregister NFS4 file layout driver with pNFS client*/
	pnfs_unregister_layoutdriver(&filelayout_type);
}

module_init(nfs4filelayout_init);
module_exit(nfs4filelayout_exit);

#endif /* CONFIG_PNFS */
