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
#include "internal.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dean Hildebrand <dhildebz@eecs.umich.edu>");
MODULE_DESCRIPTION("The NFSv4 file layout driver");

/* Callback operations to the pNFS client */
struct pnfs_client_operations *pnfs_callback_ops;

/* Forward declaration */
ssize_t filelayout_get_stripesize(struct pnfs_layout_type *);
struct layoutdriver_io_operations filelayout_io_operations;

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

	/* Retrieve and add all available devices */
	status = process_deviceid_list(fl_mt, fh, dlist);
	if (status)
		goto cleanup_mt;

	kfree(dlist);
	dprintk("%s: device list has been initialized successfully\n",
		__func__);
	return mt;

cleanup_mt: ;
	kfree(mt);

cleanup_fl_mt: ;
	kfree(fl_mt->hlist);
	kfree(fl_mt);

cleanup_dlist: ;
	kfree(dlist);

error_ret: ;
	printk(KERN_WARNING "%s: device list could not be initialized\n",
		__func__);

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

/* This function is used by the layout driver to calculate the
 * offset of the file on the dserver based on whether the
 * layout type is STRIPE_DENSE or STRIPE_SPARSE
 */
loff_t
filelayout_get_dserver_offset(loff_t offset,
			      struct nfs4_filelayout_segment *layout)
{
	if (!layout)
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


	if (rdata->orig_offset) {
		dprintk("%s new off %llu orig offset %llu\n",
			__func__, rdata->args.offset, rdata->orig_offset);
		rdata->args.offset = rdata->orig_offset;
	}

	pnfs_callback_ops->nfs_readlist_complete(rdata);
}

static void filelayout_write_call_done(struct rpc_task *task, void *data)
{
	struct nfs_write_data *wdata = (struct nfs_write_data *)data;

	if (wdata->orig_offset) {
		dprintk("%s new off %llu orig offset %llu\n",
			__func__, wdata->args.offset, wdata->orig_offset);
		wdata->args.offset = wdata->orig_offset;
	}

	pnfs_callback_ops->nfs_writelist_complete(wdata);
}

struct rpc_call_ops filelayout_read_call_ops = {
	.rpc_call_validate_args = nfs_read_validate,
	.rpc_call_done = filelayout_read_call_done,
};

struct rpc_call_ops filelayout_write_call_ops = {
	.rpc_call_validate_args = nfs_write_validate,
	.rpc_call_done = filelayout_write_call_done,
};

/* Perform sync or async reads.
 *
 * An optimization for the NFS file layout driver
 * allows the original read/write data structs to be passed in the
 * last argument.
 *
 * TODO: join with write_pagelist?
 */
static int filelayout_read_pagelist(
	struct pnfs_layout_type *layoutid,
	struct page **pages,
	unsigned int pgbase,
	unsigned nr_pages,
	loff_t offset,
	size_t count,
	struct nfs_read_data *data)
{
	struct inode *inode = PNFS_INODE(layoutid);
	struct nfs4_filelayout_segment *flseg;
	struct nfs4_pnfs_dserver dserver;
	struct nfs4_pnfs_ds *ds;
	int status;

	flseg = LSEG_LD_DATA(data->lseg);

	/* Retrieve the correct rpc_client for the byte range */
	status = nfs4_pnfs_dserver_get(data->lseg,
				       offset,
				       count,
				       &dserver);
	if (status) {
		printk(KERN_ERR "%s: dserver get failed status %d use MDS\n",
		       __func__, status);
		data->pnfs_client = NFS_CLIENT(inode);
		data->ds_nfs_client = NULL;
		data->args.fh = NFS_FH(inode);
		status = 0;
	} else {
		ds = dserver.dev->ds_list[0];

		dprintk("%s USE DS:ip %x %s\n", __func__,
			htonl(ds->ds_ip_addr), ds->r_addr);

		/* just try the first data server for the index..*/
		data->pnfs_client = ds->ds_clp->cl_rpcclient;
		data->ds_nfs_client = ds->ds_clp;
		data->args.fh = dserver.fh;

		/* Now get the file offset on the dserver
		 * Set the read offset to this offset, and
		 * save the original offset in orig_offset
		 * In the case of aync reads, the offset will be reset in the
		 * call_ops->rpc_call_done() routine.
		 */
		data->args.offset = filelayout_get_dserver_offset(offset,
								  flseg);
		data->orig_offset = offset;
	}

	/* Perform an asynchronous read */
	nfs_initiate_read(data, data->pnfs_client, &filelayout_read_call_ops);

	data->pnfs_error = 0;

	return status;
}

void
print_ds(struct nfs4_pnfs_ds *ds)
{
	dprintk("        ds->ds_ip_addr %x\n", htonl(ds->ds_ip_addr));
	dprintk("        ds->ds_port %hu\n", ntohs(ds->ds_port));
	dprintk("        ds->ds_clp %p\n", ds->ds_clp);
	dprintk("        ds->ds_count %d\n", atomic_read(&ds->ds_count));
	dprintk("        %s\n", ds->r_addr);
}

/* Perform async writes. */
static int filelayout_write_pagelist(
	struct pnfs_layout_type *layoutid,
	struct page **pages,
	unsigned int pgbase,
	unsigned nr_pages,
	loff_t offset,
	size_t count,
	int sync,
	struct nfs_write_data *data)
{
	struct inode *inode = PNFS_INODE(layoutid);
	struct nfs4_filelayout_segment *flseg = LSEG_LD_DATA(data->lseg);
	struct nfs4_pnfs_dserver dserver;
	struct nfs4_pnfs_ds *ds;
	int status;

	dprintk("--> %s ino %lu nr_pages %d pgbase %u req %Zu@%Lu sync %d\n",
		__func__, inode->i_ino, nr_pages, pgbase, count, offset, sync);

	/* Retrieve the correct rpc_client for the byte range */
	status = nfs4_pnfs_dserver_get(data->lseg,
				       offset,
				       count,
				       &dserver);

	if (status) {
		printk(KERN_ERR "%s: dserver get failed status %d use MDS\n",
		       __func__, status);
		data->pnfs_client = NFS_CLIENT(inode);
		data->ds_nfs_client = NULL;
		data->args.fh = NFS_FH(inode);
		status = 0;
	} else {
		/* use the first multipath data server */
		ds = dserver.dev->ds_list[0];

		dprintk("%s ino %lu %Zu@%Lu DS:%x:%hu %s\n",
			__func__, inode->i_ino, count, offset,
			htonl(ds->ds_ip_addr), ntohs(ds->ds_port),
			ds->r_addr);

		data->pnfs_client = ds->ds_clp->cl_rpcclient;
		data->ds_nfs_client = ds->ds_clp;
		data->args.fh = dserver.fh;

		/* Get the file offset on the dserver. Set the write offset to
		 * this offset and save the original offset.
		 */
		data->args.offset = filelayout_get_dserver_offset(offset, flseg);
		data->orig_offset = offset;
	}

	/* Perform an asynchronous write The offset will be reset in the
	 * call_ops->rpc_call_done() routine
	 */
	nfs_initiate_write(data, data->pnfs_client,
			   &filelayout_write_call_ops, sync);

	data->pnfs_error = 0;
	return 0;
}

/* Create a filelayout layout structure and return it.  The pNFS client
 * will use the pnfs_layout_type type to refer to the layout for this
 * inode from now on.
 */
struct pnfs_layout_type*
filelayout_alloc_layout(struct pnfs_mount_type *mountid, struct inode *inode)
{
	dprintk("NFS_FILELAYOUT: allocating layout\n");
	return kzalloc(sizeof(struct pnfs_layout_type) +
		       sizeof(struct nfs4_filelayout), GFP_KERNEL);
}

/* Free a filelayout layout structure
 */
void
filelayout_free_layout(struct pnfs_layout_type *layoutid)
{
	dprintk("NFS_FILELAYOUT: freeing layout\n");
	kfree(layoutid);
}

/*
 * filelayout_check_layout()
 *
 * Make sure layout segment parameters are sane WRT the device.
 *
 * Notes:
 * 1) current code insists that # stripe index = # multipath devices which
 *    is wrong.
 * 2) pattern_offset is ignored and must == 0 which is wrong;
 * 3) the pattern_offset needs to be a mutliple of the stripe unit.
*/

static int
filelayout_check_layout(struct pnfs_layout_type *lo,
			struct pnfs_layout_segment *lseg)
{
	struct nfs4_filelayout_segment *fl = LSEG_LD_DATA(lseg);
	struct nfs4_pnfs_dev_item *dev;
	int status = -EINVAL;

	dprintk("--> %s\n", __func__);
	dev = nfs4_pnfs_device_item_get(FILE_MT(lo->inode), NFS_FH(lo->inode),
					&fl->dev_id);
	if (dev == NULL) {
		dprintk("%s NO device for dev_id %s\n",
				__func__, deviceid_fmt(&fl->dev_id));
		goto out;
	}
	/* FIX-ME: need a # stripe index field */
	if (fl->first_stripe_index < 0 ||
	    fl->first_stripe_index > dev->stripe_count) {
		dprintk("%s Bad first_stripe_index %d\n",
				__func__, fl->first_stripe_index);
		goto out;
	}

	/* FIX-ME: need a # stripe index field */
	if (fl->pattern_offset != 0) {
		dprintk("%s Unsupported no-zero pattern_offset %Ld\n",
				__func__, fl->pattern_offset);
		goto out;
	}
	status = 0;
out:
	dprintk("--> %s returns %d\n", __func__, status);
	return status;
}

static void filelayout_free_lseg(struct pnfs_layout_segment *lseg);

/* Decode layout and store in layoutid.  Overwrite any existing layout
 * information for this file.
 */
static void
filelayout_set_layout(struct nfs4_filelayout *flo,
		      struct nfs4_filelayout_segment *fl,
		      struct nfs4_pnfs_layoutget_res *lgr)
{
	int i;
	uint32_t *p = (uint32_t *)lgr->layout.buf;
	uint32_t nfl_util;

	dprintk("%s: set_layout_map Begin\n", __func__);

	COPYMEM(&fl->dev_id, NFS4_PNFS_DEVICEID4_SIZE);
	READ32(nfl_util);
	if (nfl_util & NFL4_UFLG_COMMIT_THRU_MDS)
		fl->commit_through_mds = 1;
	if (nfl_util & NFL4_UFLG_DENSE)
		fl->stripe_type = STRIPE_DENSE;
	else
		fl->stripe_type = STRIPE_SPARSE;
	fl->stripe_unit = nfl_util & ~NFL4_UFLG_MASK;

	if (!flo->stripe_unit)
		flo->stripe_unit = fl->stripe_unit;
	else if (flo->stripe_unit != fl->stripe_unit) {
		printk(KERN_NOTICE "%s: updating strip_unit from %u to %u\n",
			__func__, flo->stripe_unit, fl->stripe_unit);
		flo->stripe_unit = fl->stripe_unit;
	}

	READ32(fl->first_stripe_index);
	READ64(fl->pattern_offset);
	READ32(fl->num_fh);

	dprintk("%s: nfl_util 0x%X num_fh %u fsi %u po %llu dev_id %s\n",
		__func__, nfl_util, fl->num_fh, fl->first_stripe_index,
		fl->pattern_offset, deviceid_fmt(&fl->dev_id));

	for (i = 0; i < fl->num_fh; i++) {
		/* fh */
		memset(&fl->fh_array[i], 0, sizeof(struct nfs_fh));
		READ32(fl->fh_array[i].size);
		COPYMEM(fl->fh_array[i].data, fl->fh_array[i].size);
		dprintk("DEBUG: %s: fh len %d\n", __func__,
					fl->fh_array[i].size);
	}
}

static struct pnfs_layout_segment *
filelayout_alloc_lseg(struct pnfs_layout_type *layoutid,
		      struct nfs4_pnfs_layoutget_res *lgr)
{
	struct nfs4_filelayout *flo = PNFS_LD_DATA(layoutid);
	struct pnfs_layout_segment *lseg;

	lseg = kzalloc(sizeof(struct pnfs_layout_segment) +
		       sizeof(struct nfs4_filelayout_segment), GFP_KERNEL);
	if (!lseg)
		return NULL;

	filelayout_set_layout(flo, LSEG_LD_DATA(lseg), lgr);
	if (filelayout_check_layout(layoutid, lseg)) {
		filelayout_free_lseg(lseg);
		lseg = NULL;
	}
	return lseg;
}

static void
filelayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	kfree(lseg);
}

/*
 * Allocate a new nfs_write_data struct and initialize
 */
static struct nfs_write_data *
filelayout_clone_write_data(struct nfs_write_data *old)
{
	static struct nfs_write_data *new;

	new = nfs_commit_alloc();
	if (!new)
		goto out;
	new->inode       = old->inode;
	new->cred        = old->cred;
	new->args.offset = 0;
	new->args.count  = 0;
	new->res.count   = 0;
	new->res.fattr   = &new->fattr;
	nfs_fattr_init(&new->fattr);
	new->res.verf    = &new->verf;
	new->args.context = old->args.context;
	new->call_ops = old->call_ops;
	new->how = old->how;
out:
	return new;
}

/*
 * Execute a COMMIT op to the MDS or to each data server on which a page
 * in 'pages' exists.
 * Invoke the pnfs_commit_complete callback.
 */
int
filelayout_commit(struct pnfs_layout_type *layoutid, int sync,
		  struct nfs_write_data *data)
{
	struct nfs4_filelayout_segment *nfslay;
	struct nfs_write_data   *dsdata = NULL;
	struct nfs4_pnfs_dserver dserver;
	struct nfs4_pnfs_ds *ds;
	struct nfs_page *req, *reqt;
	struct list_head *pos, *tmp, *head = &data->pages;
	loff_t file_offset, comp_offset;
	size_t stripesz, cbytes;
	int status;
	struct nfs4_pnfs_dev_item *di;
	u32 idx1, idx2;

	nfslay = LSEG_LD_DATA(data->lseg);

	dprintk("%s data %p pnfs_client %p nfslay %p sync %d\n",
		__func__, data, data->pnfs_client, nfslay, sync);

	if (nfslay->commit_through_mds) {
		dprintk("%s data %p commit through mds\n", __func__, data);
		return 1;
	}

	stripesz = filelayout_get_stripesize(layoutid);
	dprintk("%s stripesize %Zd\n", __func__, stripesz);

	di = nfs4_pnfs_device_item_get(FILE_MT(data->inode),
				       NFS_FH(data->inode), &nfslay->dev_id);
	if (di == NULL) {
		status = -EIO;
		goto out_bad;
	}

	/* COMMIT to each Data Server */
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);

		file_offset = req->wb_index << PAGE_CACHE_SHIFT;

		/* Get dserver for the current page */
		status = nfs4_pnfs_dserver_get(data->lseg,
					       file_offset,
					       req->wb_bytes,
					       &dserver);

		/* Get its index */
		idx1 = filelayout_dserver_get_index(file_offset, di, nfslay);

		if (status) {
			status = -EIO;
			goto out_bad;
		}

		dsdata = filelayout_clone_write_data(data);
		if (!dsdata) {
			status = -ENOMEM;
			goto out_bad;
		}

		/* Just try the first multipath data server */
		ds = dserver.dev->ds_list[0];
		dsdata->pnfs_client = ds->ds_clp->cl_rpcclient;
		dsdata->ds_nfs_client = ds->ds_clp;
		dsdata->args.fh = dserver.fh;
		cbytes = req->wb_bytes;

		/* Gather all pages going to the current data server by
		 * comparing their indices.
		 * XXX: This recalculates the indices unecessarily.
		 *      One idea would be to calc the index for every page
		 *      and then compare if they are the same. */
		list_for_each_safe(pos, tmp, head) {
			reqt = nfs_list_entry(pos);
			comp_offset = reqt->wb_index << PAGE_CACHE_SHIFT;
			idx2 = filelayout_dserver_get_index(comp_offset, di, nfslay);
			if (idx1 == idx2) {
				nfs_list_remove_request(reqt);
				nfs_list_add_request(reqt, &dsdata->pages);
				cbytes += reqt->wb_bytes;
			}
		}

		dprintk("%s: Initiating commit: %Zu@%llu USE DS:\n",
			__func__, cbytes, file_offset);
		print_ds(ds);

		/* Send COMMIT to data server */
		nfs_initiate_commit(dsdata, dsdata->pnfs_client, sync);
	}
	/* Release original commit data since it is not used */
	nfs_commit_free(data);
	return 0;

out_bad:
	printk(KERN_ERR "%s: dserver get failed status %d\n", __func__, status);

	/* XXX should we send COMMIT to MDS e.g. not free data and return 1 ? */
	nfs_commit_free(data);
	return status;
}

/* Return the stripesize for the specified file.
 */
ssize_t
filelayout_get_stripesize(struct pnfs_layout_type *layoutid)
{
	struct nfs4_filelayout *flo = PNFS_LD_DATA(layoutid);

	return flo->stripe_unit;
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

#if 0
	dprintk("%s p %u r %u bnd %d bsize %Zu\n",__func__, p_stripe, r_stripe, pgio->pg_boundary, pgio->pg_bsize);
#endif

	return (p_stripe == r_stripe);
}

/* Issue a layoutget in the same compound as OPEN
 */
int
filelayout_layoutget_on_open(struct pnfs_mount_type *mountid)
{
	return 1;
}

ssize_t
filelayout_get_io_threshold(struct pnfs_layout_type *layoutid,
			    struct inode *inode)
{
	return -1;
}

static int
filelayout_use_rpc_code(void)
{
	return 1;
}

struct layoutdriver_io_operations filelayout_io_operations = {
	.commit                  = filelayout_commit,
	.read_pagelist           = filelayout_read_pagelist,
	.write_pagelist          = filelayout_write_pagelist,
	.alloc_layout            = filelayout_alloc_layout,
	.free_layout             = filelayout_free_layout,
	.alloc_lseg              = filelayout_alloc_lseg,
	.free_lseg               = filelayout_free_lseg,
	.initialize_mountpoint   = filelayout_initialize_mountpoint,
	.uninitialize_mountpoint = filelayout_uninitialize_mountpoint,
};

struct layoutdriver_policy_operations filelayout_policy_operations = {
	.get_stripesize        = filelayout_get_stripesize,
	.gather_across_stripes = filelayout_gather_across_stripes,
	.pg_test               = filelayout_pg_test,
	.layoutget_on_open     = filelayout_layoutget_on_open,
	.get_read_threshold    = filelayout_get_io_threshold,
	.get_write_threshold   = filelayout_get_io_threshold,
	.use_rpc_code          = filelayout_use_rpc_code,
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
	       __func__);

	/* Need to register file_operations struct with global list to indicate
	* that NFS4 file layout is a possible pNFS I/O module
	*/
	pnfs_callback_ops = pnfs_register_layoutdriver(&filelayout_type);

	return 0;
}

static void __exit nfs4filelayout_exit(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Unregistering...\n",
	       __func__);

	/* Unregister NFS4 file layout driver with pNFS client*/
	pnfs_unregister_layoutdriver(&filelayout_type);
}

module_init(nfs4filelayout_init);
module_exit(nfs4filelayout_exit);

#endif /* CONFIG_PNFS */
