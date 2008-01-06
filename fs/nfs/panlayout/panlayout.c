/*
 *  panlayout.c
 *
 *  pNFS layout driver for Panasas OSDs
 *
 *  Copyright (C) 2007 Panasas Inc.
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
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
 *  3. Neither the name of the Panasas company nor the names of its
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
 *
 * See the file COPYING included with this distribution for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/nfs_fs.h>

#include "panlayout.h"
#include "pnfs_osd_xdr.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS

MODULE_DESCRIPTION("pNFS Layout Driver for Panasas OSDs");
MODULE_AUTHOR("Benny Halevy <bhalevy@panasas.com>");
MODULE_LICENSE("GPL");

static struct pnfs_client_operations *pnfs_client_ops;

/*
 * Create a panlayout layout structure for the given inode and return it.
 */
static struct pnfs_layout_type *
panlayout_alloc_layout(struct pnfs_mount_type *mountid, struct inode *inode)
{
	struct pnfs_layout_type *pnfslay;

	pnfslay = kzalloc(sizeof(struct pnfs_layout_type) +
			  sizeof(struct panlayout), GFP_KERNEL);
	dprintk("%s: Return %p\n", __func__, pnfslay);
	return pnfslay;
}

/*
 * Free a panlayout layout structure
 */
static void
panlayout_free_layout(struct pnfs_layout_type *pnfslay)
{
	dprintk("%s: pnfslay %p\n", __func__, pnfslay);
	kfree(pnfslay);
}

/*
 * Unmarshall layout and store it in pnfslay.
 */
static struct pnfs_layout_segment *
panlayout_alloc_lseg(struct pnfs_layout_type *pnfslay,
		     struct nfs4_pnfs_layoutget_res *lgr)
{
	int status;
	void *layout = lgr->layout.buf;
	struct pnfs_layout_segment *lseg;
	struct panlayout_segment *panlseg;
	struct pnfs_osd_layout *pnfs_osd_layout;

	dprintk("%s: Begin pnfslay %p layout %p\n", __func__, pnfslay, layout);

	BUG_ON(!layout);

	status = -ENOMEM;
	lseg = kzalloc(sizeof(*lseg) + sizeof(*panlseg) +
		       pnfs_osd_layout_incore_sz(layout), GFP_KERNEL);
	if (!lseg)
		goto err;

	panlseg = LSEG_LD_DATA(lseg);
	pnfs_osd_layout = (struct pnfs_osd_layout *)panlseg->pnfs_osd_layout;
	pnfs_osd_xdr_decode_layout(pnfs_osd_layout, layout);

	status = panfs_shim_conv_layout(&panlseg->panfs_internal, lseg,
					pnfs_osd_layout);
	if (status)
		goto err;

	dprintk("%s: Return %p\n", __func__, lseg);
	return lseg;

 err:
	kfree(lseg);
	return ERR_PTR(status);
}

/*
 * Free a layout segement
 */
static void
panlayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct panlayout_segment *panlseg;

	dprintk("%s: freeing layout segment %p\n", __func__, lseg);

	if (unlikely(!lseg))
		return;

	panlseg = LSEG_LD_DATA(lseg);
	panfs_shim_free_layout(panlseg->panfs_internal);
	kfree(lseg);
}

/*
 * I/O Operations
 */

static struct panlayout_io_state *
panlayout_alloc_io_state(void)
{
	struct panlayout_io_state *p;
	dprintk("%s: allocating io_state\n", __func__);
	if (panfs_shim_alloc_io_state(&p))
		return NULL;
	return p;
}

static void
panlayout_free_io_state(struct panlayout_io_state *state)
{
	dprintk("%s: freeing io_state\n", __func__);
	if (unlikely(!state))
		return;

	panfs_shim_free_io_state(state);
}

/*
 * I/O done
 *
 * Dereference a layout segment and decrement io in-progress counter
 * Free layout segment is ref count reached zero
 */
static void
panlayout_iodone(struct panlayout_io_state *state)
{
	dprintk("%s: state %p\n", __func__, state);
	panlayout_free_io_state(state);
}

/*
 * Commit data remotely on OSDs
 */
int
panlayout_commit(struct pnfs_layout_type *pnfslay,
		 int sync,
		 struct nfs_write_data *data)
{
	int status = 0;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

void
panlayout_read_done(struct panlayout_io_state *state)
{
}

/*
 * Perform sync or async reads.
 */
int
panlayout_read_pagelist(struct pnfs_layout_type *pnfs_layout_type,
			struct page **pages,
			unsigned pgbase,
			unsigned nr_pages,
			loff_t offset,
			size_t count,
			struct nfs_read_data *rdata)
{
	int status = -EIO;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

void
panlayout_write_done(struct panlayout_io_state *state)
{
}

/*
 * Perform sync or async writes.
 */
int
panlayout_write_pagelist(struct pnfs_layout_type *pnfs_layout_type,
			 struct page **pages,
			 unsigned pgbase,
			 unsigned nr_pages,
			 loff_t offset,
			 size_t count,
			 int stable,
			 struct nfs_write_data *wdata)
{
	int status = -EIO;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

int
panlayout_setup_layoutcommit(struct pnfs_layout_type *pnfslay,
			     struct pnfs_layoutcommit_arg *arg)
{
	int status = -EIO;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

void
panlayout_cleanup_layoutcommit(struct pnfs_layout_type *pnfslay,
			       struct pnfs_layoutcommit_arg *arg,
			       struct pnfs_layoutcommit_res *res)
{
	dprintk("%s: Return\n", __func__);
}

/*
 * Initialize a mountpoint by retrieving the list of
 * available devices for it.
 * Return the pnfs_mount_type structure so the
 * pNFS_client can refer to the mount point later on.
 */
static struct pnfs_mount_type *
panlayout_initialize_mountpoint(struct super_block *sb, struct nfs_fh *fh)
{
	struct pnfs_mount_type *mt = NULL;

	dprintk("%s: Return %p\n", __func__, mt);
	return mt;
}

/*
 * Uninitialize a mountpoint
 */
static int
panlayout_uninitialize_mountpoint(struct pnfs_mount_type *mt)
{
	dprintk("%s: Begin %p\n", __func__, mt);
	return -EIO;
}

static struct layoutdriver_io_operations panlayout_io_operations = {
	.commit                  = panlayout_commit,
	.read_pagelist           = panlayout_read_pagelist,
	.write_pagelist          = panlayout_write_pagelist,
	.alloc_layout            = panlayout_alloc_layout,
	.free_layout             = panlayout_free_layout,
	.alloc_lseg              = panlayout_alloc_lseg,
	.free_lseg               = panlayout_free_lseg,
	.setup_layoutcommit      = panlayout_setup_layoutcommit,
	.cleanup_layoutcommit    = panlayout_cleanup_layoutcommit,
	.initialize_mountpoint   = panlayout_initialize_mountpoint,
	.uninitialize_mountpoint = panlayout_uninitialize_mountpoint,
};

/*
 * Policy Operations
 */

/*
 * Return the stripe size for the specified file
 */
ssize_t
panlayout_get_stripesize(struct pnfs_layout_type *pnfslay)
{
	ssize_t maxsz = -1;
	dprintk("%s: Return %Zd\n", __func__, maxsz);
	return maxsz;
}

/*
 * Don't gather across stripes, but rather gather (coalesce) up to
 * the stripe size.
 *
 * FIXME: change interface to use merge_align, merge_count
 */
static int
panlayout_gather_across_stripes(struct pnfs_mount_type *mountid)
{
	int status = 0;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

/*
 * Get the max [rw]size
 */
static ssize_t
panlayout_get_blocksize(struct pnfs_mount_type *mountid)
{
	ssize_t sz = -1;
	dprintk("%s: Return %Zd\n", __func__, sz);
	return sz;
}

/*
 * Get the I/O threshold
 */
static ssize_t
panlayout_get_io_threshold(struct pnfs_layout_type *layoutid,
			   struct inode *inode)
{
	ssize_t sz = -1;
	dprintk("%s: Return %Zd\n", __func__, sz);
	return sz;
}

/*
 * Issue a layoutget in the same compound as OPEN
 */
static int
panlayout_layoutget_on_open(struct pnfs_mount_type *mountid)
{
	int status = -1;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

/*
 * Commit and return the layout upon a setattr
 */
static int
panlayout_layoutret_on_setattr(struct pnfs_mount_type *mountid)
{
	int status = 1;
	dprintk("%s: Return %d\n", __func__, status);
	return status;
}

static struct layoutdriver_policy_operations panlayout_policy_operations = {
	.get_stripesize        = panlayout_get_stripesize,
	.gather_across_stripes = panlayout_gather_across_stripes,
	.get_blocksize         = panlayout_get_blocksize,
	.get_read_threshold    = panlayout_get_io_threshold,
	.get_write_threshold   = panlayout_get_io_threshold,
	.layoutget_on_open     = panlayout_layoutget_on_open,
	.layoutret_on_setattr  = panlayout_layoutret_on_setattr,
};

static struct pnfs_layoutdriver_type panlayout_type = {
	.id = PNFS_LAYOUT_PANOSD,
	.name = "PNFS_LAYOUT_PANOSD",
	.ld_io_ops = &panlayout_io_operations,
	.ld_policy_ops = &panlayout_policy_operations,
};

static int __init
panlayout_init(void)
{
	pnfs_client_ops = pnfs_register_layoutdriver(&panlayout_type);
	printk(KERN_INFO "%s: Registered Panasas OSD pNFS Layout Driver\n",
	       __func__);
	return 0;
}

static void __exit
panlayout_exit(void)
{
	pnfs_unregister_layoutdriver(&panlayout_type);
	printk(KERN_INFO "%s: Unregistered Panasas OSD pNFS Layout Driver\n",
	       __func__);
}

module_init(panlayout_init);
module_exit(panlayout_exit);
