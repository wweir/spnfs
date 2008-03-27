/*
 *  linux/fs/nfs/blocklayout/blocklayout.c
 *
 *  Module for the NFSv4.1 pNFS block layout driver.
 *
 *  Copyright (c) 2006 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@citi.umich.edu>
 *  Fred Isaman <iisaman@umich.edu>
 *
 * permission is granted to use, copy, create derivative works and
 * redistribute this software and such derivative works for any purpose,
 * so long as the name of the university of michigan is not used in
 * any advertising or publicity pertaining to the use or distribution
 * of this software without specific, written prior authorization.  if
 * the above copyright notice or any other identification of the
 * university of michigan is included in any copy of any portion of
 * this software, then the disclaimer below must also be included.
 *
 * this software is provided as is, without representation from the
 * university of michigan as to its fitness for any purpose, and without
 * warranty by the university of michigan of any kind, either express
 * or implied, including without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  the regents
 * of the university of michigan shall not be liable for any damages,
 * including special, indirect, incidental, or consequential damages,
 * with respect to any claim arising out or in connection with the use
 * of the software, even if it has been or is hereafter advised of the
 * possibility of such damages.
 */
#include <linux/module.h>
#include <linux/init.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

/* Callback operations to the pNFS client */
struct pnfs_client_operations *pnfs_callback_ops;

static void
destroy_extent(struct kref *kref)
{
	struct pnfs_block_extent *be;

	be = container_of(kref, struct pnfs_block_extent, be_refcnt);
	dprintk("%s be=%p\n", __func__, be);
	kfree(be);
}

static void
put_extent(struct pnfs_block_extent *be)
{
	if (be) {
		dprintk("%s enter %p (%i)\n", __func__, be,
			atomic_read(&be->be_refcnt.refcount));
		kref_put(&be->be_refcnt, destroy_extent);
	}
}

static int
bl_commit(struct pnfs_layout_type *layoutid,
		int sync,
		struct nfs_write_data *nfs_data)
{
	dprintk("%s enter\n", __func__);
	/* Curently, this is only allowed to return:
	 *   0 - success
	 *   1 - fall back to non-pnfs commit
	 */
	return 1;
}

static int
bl_read_pagelist(struct pnfs_layout_type *layoutid,
		struct page **pages,
		unsigned int pgbase,
		unsigned nr_pages,
		loff_t offset,
		size_t count,
		struct nfs_read_data *nfs_data)
{
	dprintk("%s enter\n", __func__);
	return 1;
}

/* FRED - this should return just 0 (to indicate done for now)
 * or 1 (to indicate try normal nfs).  It can indicate bytes
 * written in wdata->res.count.  It can indicate error status in
 * wdata->task.tk_status.
 */
static int
bl_write_pagelist(struct pnfs_layout_type *layoutid,
		struct page **pages,
		unsigned int pgbase,
		unsigned nr_pages,
		loff_t offset,
		size_t count,
		int sync,
		struct nfs_write_data *wdata)
{
	dprintk("%s enter - just using nfs\n", __func__);
	return 1;
}

static void
release_extents(struct pnfs_block_layout *bl)
{
	struct pnfs_block_extent *be;

	spin_lock(&bl->bl_ext_lock);
	while (!list_empty(&bl->bl_extents)) {
		be = list_first_entry(&bl->bl_extents, struct pnfs_block_extent,
				      be_node);
		list_del(&be->be_node);
		put_extent(be);
	}
	bl->bl_n_ext = 0;
	spin_unlock(&bl->bl_ext_lock);
}

static void
bl_free_layout(struct pnfs_layout_type *lt)
{
	dprintk("%s enter\n", __func__);
	kfree(lt);
	return;
}

/* XXX Ignoring ld_data for the moment */
static struct pnfs_layout_type *
bl_alloc_layout(struct pnfs_mount_type *mtype, struct inode *inode)
{
	struct pnfs_layout_type		*lt;

	dprintk("%s enter\n", __func__);
	lt = kzalloc(sizeof(*lt) + 0, GFP_KERNEL);
	return lt;
}

static void
bl_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct pnfs_block_layout *bl;

	dprintk("%s enter\n", __func__);
	if (lseg) {
		bl = (struct pnfs_block_layout *)lseg->ld_data;
		release_extents(bl);
		kfree(lseg);
	}
	return;
}

static struct pnfs_layout_segment *
bl_alloc_lseg(struct pnfs_layout_type *layoutid,
	      struct nfs4_pnfs_layoutget_res *lgr)
{
	struct pnfs_layout_segment *lseg;
	struct pnfs_block_layout *bl;
	int status;

	dprintk("%s enter\n", __func__);
	lseg = kzalloc(sizeof(*lseg) + sizeof(*bl), GFP_KERNEL);
	if (!lseg)
		return NULL;
	bl = (struct pnfs_block_layout *) lseg->ld_data;
	/* This is needed to get layoutid->ld_data (metadevice list) from bl */
	lseg->layout = layoutid;

	spin_lock_init(&bl->bl_ext_lock);
	INIT_LIST_HEAD(&bl->bl_extents);

	status = nfs4_blk_process_layoutget(bl, lgr);
	if (status) {
		bl_free_lseg(lseg);
		return ERR_PTR(status);
	}
	return lseg;
}

static int
bl_setup_layoutcommit(struct pnfs_layout_type *layoutid,
		struct pnfs_layoutcommit_arg *arg)
{
	dprintk("%s enter\n", __func__);
	return 0;
}

static void
bl_cleanup_layoutcommit(struct pnfs_layout_type *layoutid,
		struct pnfs_layoutcommit_arg *arg,
		struct pnfs_layoutcommit_res *res)
{
	dprintk("%s enter\n", __func__);
}

static void free_blk_mountid(struct block_mount_id *mid)
{
	if (mid) {
		struct pnfs_block_dev *dev;
		spin_lock(&mid->bm_lock);
		while (!list_empty(&mid->bm_devlist)) {
			dev = list_first_entry(&mid->bm_devlist,
					       struct pnfs_block_dev,
					       bm_node);
			list_del(&dev->bm_node);
			free_block_dev(dev);
		}
		spin_unlock(&mid->bm_lock);
		kfree(mid);
	}
}

/* Send and process GETDEVICEINFO for given deviceid.  We search for
 * device sigs among drives in the sdlist.
 */
static struct pnfs_block_dev *
nfs4_blk_get_deviceinfo(struct super_block *sb, struct nfs_fh *fh,
			struct pnfs_deviceid *d_id,
			struct list_head *sdlist)
{
	struct pnfs_device *dev;
	struct pnfs_block_dev *rv = NULL;

	dprintk("%s enter\n", __func__);
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dprintk("%s kmalloc failed\n", __func__);
		return NULL;
	}
	memcpy(&dev->dev_id, d_id, sizeof(*dev));
	dev->layout_type = LAYOUT_BLOCK_VOLUME;
	dev->dev_notify_types = 0;
	if (!pnfs_callback_ops->nfs_getdeviceinfo(sb, fh, dev))
		rv = nfs4_blk_decode_device(sb, dev, sdlist);
	kfree(dev);
	return rv;
}


/*
 * Retrieve the list of available devices for the mountpoint.
 */
static struct pnfs_mount_type *
bl_initialize_mountpoint(struct super_block *sb, struct nfs_fh *fh)
{
	struct block_mount_id *b_mt_id = NULL;
	struct pnfs_mount_type *mtype = NULL;
	struct pnfs_devicelist *dlist = NULL;
	struct pnfs_block_dev *bdev;
	LIST_HEAD(scsi_disklist);
	int status, i;

	dprintk("%s enter\n", __func__);

	b_mt_id = kzalloc(sizeof(struct block_mount_id), GFP_KERNEL);
	if (!b_mt_id)
		goto out_error;
	/* Initialize nfs4 block layout mount id */
	b_mt_id->bm_sb = sb; /* back pointer to retrieve nfs_server struct */
	spin_lock_init(&b_mt_id->bm_lock);
	INIT_LIST_HEAD(&b_mt_id->bm_devlist);
	mtype = kzalloc(sizeof(struct pnfs_mount_type), GFP_KERNEL);
	if (!mtype)
		goto out_error;
	mtype->mountid = (void *)b_mt_id;

	/* Construct a list of all visible scsi disks that have not been
	 * claimed.
	 */
	status =  nfs4_blk_create_scsi_disk_list(&scsi_disklist);
	if (status < 0)
		goto out_error;

	dlist = kmalloc(sizeof(struct pnfs_devicelist), GFP_KERNEL);
	if (!dlist)
		goto out_error;
	dlist->eof = 0;
	while (!dlist->eof) {
		status = pnfs_callback_ops->nfs_getdevicelist(sb, fh, dlist);
		if (status)
			goto out_error;
		dprintk("%s GETDEVICELIST numdevs=%i, eof=%i\n",
			__func__, dlist->num_devs, dlist->eof);
		/* For each device returned in dlist, call GETDEVICEINFO, and
		 * decode the opaque topology encoding to create a flat
		 * volume topology, matching VOLUME_SIMPLE disk signatures
		 * to disks in the visible scsi disk list.
		 * Construct an LVM meta device from the flat volume topology.
		 */
		for (i = 0; i < dlist->num_devs; i++) {
			bdev = nfs4_blk_get_deviceinfo(sb, fh,
						     &dlist->dev_id[i],
						     &scsi_disklist);
			if (!bdev)
				goto out_error;
			spin_lock(&b_mt_id->bm_lock);
			list_add(&bdev->bm_node, &b_mt_id->bm_devlist);
			spin_unlock(&b_mt_id->bm_lock);
		}
	}
	dprintk("%s SUCCESS\n", __func__);

 out_return:
	kfree(dlist);
	nfs4_blk_destroy_disk_list(&scsi_disklist);
	return mtype;

 out_error:
	free_blk_mountid(b_mt_id);
	kfree(mtype);
	mtype = NULL;
	goto out_return;
}

static int
bl_uninitialize_mountpoint(struct pnfs_mount_type *mtype)
{
	struct block_mount_id *b_mt_id = NULL;

	dprintk("%s enter\n", __func__);
	if (!mtype)
		return 0;
	b_mt_id = (struct block_mount_id *)mtype->mountid;
	free_blk_mountid(b_mt_id);
	kfree(mtype);
	dprintk("%s RETURNS\n", __func__);
	return 0;
}

static ssize_t
bl_get_stripesize(struct pnfs_layout_type *layoutid)
{
	dprintk("%s enter\n", __func__);
	return 0;
}

static ssize_t
bl_get_io_threshold(struct pnfs_layout_type *layoutid, struct inode *inode)
{
	dprintk("%s enter\n", __func__);
	return 0;
}

/* This is called by nfs_can_coalesce_requests via nfs_pageio_do_add_request.
 * Should return False if there is a reason requests can not be coalesced,
 * otherwise, should default to returning True.
 */
static int
bl_pg_test(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
	   struct nfs_page *req)
{
	dprintk("%s enter\n", __func__);
	return 1;
}

static struct layoutdriver_io_operations blocklayout_io_operations = {
	.commit				= bl_commit,
	.read_pagelist			= bl_read_pagelist,
	.write_pagelist			= bl_write_pagelist,
	.alloc_layout			= bl_alloc_layout,
	.free_layout			= bl_free_layout,
	.alloc_lseg			= bl_alloc_lseg,
	.free_lseg			= bl_free_lseg,
	.setup_layoutcommit		= bl_setup_layoutcommit,
	.cleanup_layoutcommit		= bl_cleanup_layoutcommit,
	.initialize_mountpoint		= bl_initialize_mountpoint,
	.uninitialize_mountpoint	= bl_uninitialize_mountpoint,
};

static struct layoutdriver_policy_operations blocklayout_policy_operations = {
	.get_stripesize			= bl_get_stripesize,
	.get_read_threshold		= bl_get_io_threshold,
	.get_write_threshold		= bl_get_io_threshold,
	.pg_test			= bl_pg_test,
};

static struct pnfs_layoutdriver_type blocklayout_type = {
	.id = LAYOUT_BLOCK_VOLUME,
	.name = "LAYOUT_BLOCK_VOLUME",
	.ld_io_ops = &blocklayout_io_operations,
	.ld_policy_ops = &blocklayout_policy_operations,
};

static int __init nfs4blocklayout_init(void)
{
	dprintk("%s: NFSv4 Block Layout Driver Registering...\n", __func__);

	pnfs_callback_ops = pnfs_register_layoutdriver(&blocklayout_type);
	return 0;
}

static void __exit nfs4blocklayout_exit(void)
{
	dprintk("%s: NFSv4 Block Layout Driver Unregistering...\n",
	       __func__);

	pnfs_unregister_layoutdriver(&blocklayout_type);
}

module_init(nfs4blocklayout_init);
module_exit(nfs4blocklayout_exit);
