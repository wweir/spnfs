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

#include <linux/nfs_fs.h>
#include <linux/pnfs_xdr.h> /* Needed by nfs4_pnfs.h */
#include <linux/nfs4_pnfs.h>

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

/* Callback operations to the pNFS client */
struct pnfs_client_operations *pnfs_callback_ops;

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
bl_free_layout(struct pnfs_layout_type *layoutid)
{
	dprintk("%s enter\n", __func__);
	return;
}

static struct pnfs_layout_type *
bl_alloc_layout(struct pnfs_mount_type *mtype, struct inode *inode)
{
	dprintk("%s enter\n", __func__);
	return NULL;
}

static void
bl_free_lseg(struct pnfs_layout_segment *lseg)
{
	dprintk("%s enter\n", __func__);
	return;
}

static struct pnfs_layout_segment *
bl_alloc_lseg(struct pnfs_layout_type *layoutid,
	      struct nfs4_pnfs_layoutget_res *lgr)
{
	dprintk("%s enter\n", __func__);
	return NULL;
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

static struct pnfs_mount_type *
bl_initialize_mountpoint(struct super_block *sb, struct nfs_fh *fh)
{
	dprintk("%s enter\n", __func__);
	return NULL;
}

static int
bl_uninitialize_mountpoint(struct pnfs_mount_type *mtype)
{
	dprintk("%s enter\n", __func__);
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
