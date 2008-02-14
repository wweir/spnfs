/*
 *  linux/fs/nfs/nfs4blocklayout.c
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

#include <linux/buffer_head.h> /* DEBUG Needed for print_page calls */
#include <linux/bio.h> /* struct bio */
#include "nfs4blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_BLOCKLAYOUT

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

/* Callback operations to the pNFS client */
struct pnfs_client_operations *pnfs_callback_ops;

static void print_page(struct page *page)
{
	dprintk("PRINTPAGE page %p\n", page);
	dprintk("        PagePrivate %d\n", PagePrivate(page));
	dprintk("        PageUptodate %d\n", PageUptodate(page));
	dprintk("        PageError %d\n", PageError(page));
	dprintk("        PageDirty %d\n", PageDirty(page));
	dprintk("        PageReferenced %d\n", PageReferenced(page));
	dprintk("        PageLocked %d\n", PageLocked(page));
	dprintk("        PageWriteback %d\n", PageWriteback(page));
	dprintk("        PageMappedToDisk %d\n", PageMappedToDisk(page));
	dprintk("\n");
}

static void print_bl_extent(struct pnfs_block_extent *be)
{
	dprintk("PRINT EXTENT extent %p\n", be);
	if (be) {
		dprintk("        be_f_offset %Lu\n", be->be_f_offset);
		dprintk("        be_length   %Lu\n", be->be_length);
		dprintk("        be_v_offset %Lu\n", be->be_v_offset);
		dprintk("        be_state    %d\n", be->be_state);
	}
}

static int
dont_like_caller(struct nfs_page *req)
{
	if (atomic_read(&req->wb_complete)) {
		/* Called by _multi */
		return 1;
	} else {
		/* Called by _one */
		return 0;
	}
}

static void
destroy_extent(struct kref *kref)
{
	struct pnfs_block_extent *be;

	be = container_of(kref, struct pnfs_block_extent, be_refcnt);
	dprintk("%s be=%p\n", __FUNCTION__, be);
	kfree(be);
}

static void
put_extent(struct pnfs_block_extent *be)
{
	if (be) {
		dprintk("%s enter %p (%i)\n", __FUNCTION__, be,
			atomic_read(&be->be_refcnt.refcount));
		kref_put(&be->be_refcnt, destroy_extent);
	}
}

static int
bl_commit(struct pnfs_layout_type *layoutid,
		int sync,
		struct nfs_write_data *nfs_data)
{
	dprintk("%s enter\n", __FUNCTION__);
	/* Curently, this is only allowed to return:
	 *   0 - success
	 *   1 - fall back to non-pnfs commit
	 */
	return 1;
}

static void bl_readlist_done(struct nfs_read_data *rdata, int status)
{
	/* STUB - need to think through what to put into rdata */
	rdata->task.tk_status = status;
	rdata->res.eof = 0;
	rdata->res.count = (status ? 0 : rdata->args.count);
	pnfs_callback_ops->nfs_readlist_complete(rdata);
}

static void bl_end_read_bio(struct bio *bio, int err)
{
	struct nfs_read_data *data = (struct nfs_read_data *)bio->bi_private;

	dprintk("%s called with err=%i\n", __FUNCTION__, err);
	bl_readlist_done(data, err);
	bio_put(bio);
}

/* Returns extent, or NULL.  If a second READ extent exists, it is returned
 * in cow_read, if given.
 *
 * We assume about the extent list:
 * 1. Extents are ordered by file offset, if two extents have same offset,
 *    we don't care about ordering.
 * 2. For any given isect, there are at most two extents that match.
 * 3. If two extents match, exactly one will have state==READ_DATA
 */
struct pnfs_block_extent *
find_get_extent(struct pnfs_layout_segment *lseg, sector_t isect,
	    struct pnfs_block_extent **cow_read)
{
	struct pnfs_block_layout *bl = BLK_LO(lseg);
	struct pnfs_block_extent *be, *cow, *out;

	dprintk("%s enter with isect %Ld\n", __FUNCTION__, isect);
	cow = out = NULL;
	spin_lock(&bl->bl_ext_lock);
	list_for_each_entry(be, &bl->bl_extents, be_node) {
		if (isect < be->be_f_offset)
			break;
		if (isect < be->be_f_offset + be->be_length) {
			/* We have found an extent, now decide if it should
			 * be returned in cow_read or not.
			 */
			dprintk("%s Get %p (%i)\n", __FUNCTION__, be,
				atomic_read(&be->be_refcnt.refcount));
			kref_get(&be->be_refcnt);
			if (!out)
				out = be;
			else {
				if (out->be_state == READ_DATA) {
					cow = out;
					out = be;
				} else
					cow = be;
				break;
			}
		}
	}
	spin_unlock(&bl->bl_ext_lock);
	if (cow_read)
		*cow_read = cow;
	else
		put_extent(cow);
	print_bl_extent(out);
	return out;
}

/* Given the be associated with isect, determine if page data needs to be
 * initialized.
 */
static int is_hole(struct pnfs_block_extent *be, sector_t isect)
{
	if (be->be_state == INVALID_DATA || be->be_state == NONE_DATA)
		return 1;
	else if (be->be_state != NEEDS_INIT)
		return 0;
	else {
		uint32_t mask;
		mask = 1 << ((isect - be->be_f_offset) >>
			     (PAGE_CACHE_SHIFT - 9));
		return be->be_bitmap & mask;
	}
}

static int
bl_read_pagelist(struct pnfs_layout_type *layoutid,
		struct page **pages,
		unsigned int pgbase,
		unsigned nr_pages,
		loff_t f_offset,
		size_t count,
		struct nfs_read_data *rdata)
{
	int i, hole;
	struct bio *bio;
	struct pnfs_block_extent *be = NULL, *cow_read = NULL;
	sector_t isect;

	dprintk("%s enter nr_pages %u offset %Ld count %d\n", __FUNCTION__,
	       nr_pages, f_offset, count);

	if (f_offset & 0x1ff) {
		/* This shouldn't be needed, just being paranoid */
		int diff;
		dprintk("%s f_offset %Ld not aligned\n",
			__FUNCTION__, f_offset);
		diff = f_offset & 0x1ff;
		f_offset &= ~0x1ff;
		count += diff;
	}
	if (dont_like_caller(rdata->req)) {
		dprintk("%s dont_like_caller failed\n", __FUNCTION__);
		goto use_mds;
	}
	isect = (sector_t) (f_offset >> 9);
	be = find_get_extent(rdata->lseg, isect, &cow_read);
	if (!be || count > (be->be_length << 9)) {
		/* STUB - if count is large, should break into
		 * multiple bios. Also, need to check cow_read size.
		 */
		goto use_mds;
	}
	hole = is_hole(be, isect);
	if (hole && !cow_read) {
		/* Fill hole w/ zeroes w/o accessing device */
		dprintk("%s Zeroing pages for hole\n", __FUNCTION__);
		for (i = 0; i < nr_pages; i++) {
			zero_user_page(pages[i], 0,
				       min_t(int, PAGE_CACHE_SIZE, count),
				       KM_USER0);
			print_page(pages[i]);
			count -= PAGE_CACHE_SIZE;
		}
		bl_readlist_done(rdata, 0);
	} else {
		struct pnfs_block_extent *be_read;
		int added;
		be_read = hole && cow_read ? cow_read : be;
		bio = bio_alloc(GFP_NOIO, nr_pages);
		bio->bi_sector = isect - be_read->be_f_offset +
			be_read->be_v_offset;
		bio->bi_bdev = BLK_ID(layoutid)->bm_mdev;
		bio->bi_end_io = bl_end_read_bio;
		bio->bi_private = rdata;
		for (i = 0; i < nr_pages; i++) {
			added = bio_add_page(bio, pages[i], PAGE_SIZE, 0);
			if (added < PAGE_SIZE) {
				dprintk("%s bio_add_page(%lu)=%i\n",
					__FUNCTION__, PAGE_SIZE, added);
				bio_put(bio);
				goto use_mds;
			}
		}
		dprintk("%s submitting read bio\n", __FUNCTION__);
		submit_bio(READ, bio);
	}
	put_extent(be);
	put_extent(cow_read);
	return 0;

 use_mds:
	dprintk("Giving up and using normal NFS\n");
	put_extent(be);
	put_extent(cow_read);
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
	dprintk("%s enter - just using nfs\n", __FUNCTION__);
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
	dprintk("%s enter\n", __FUNCTION__);
	kfree(lt);
	return;
}

/* XXX Ignoring ld_data for the moment */
static struct pnfs_layout_type *
bl_alloc_layout(struct pnfs_mount_type *mtype, struct inode *inode)
{
	struct pnfs_layout_type		*lt;

	dprintk("%s enter\n", __FUNCTION__);
	lt = kzalloc(sizeof(*lt) + 0, GFP_KERNEL);
	return lt;
}

static void
bl_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct pnfs_block_layout *bl;

	dprintk("%s enter\n", __FUNCTION__);
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

	dprintk("%s enter\n", __FUNCTION__);
	lseg = kzalloc(sizeof(*lseg) + sizeof(*bl), GFP_KERNEL);
	if (!lseg)
		return NULL;
	bl = (struct pnfs_block_layout *) lseg->ld_data;
	/* XXX bl is going to change */
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
	dprintk("%s enter\n", __FUNCTION__);
	return 0;
}

static void
bl_cleanup_layoutcommit(struct pnfs_layout_type *layoutid,
		struct pnfs_layoutcommit_arg *arg,
		struct pnfs_layoutcommit_res *res)
{
	dprintk("%s enter\n", __FUNCTION__);
}

/* Right now this is called without lock held.
 * XXX Does it make more sense to call with lock held?
 */
static void free_blk_mountid(struct block_mount_id *b_mt_id)
{
	if (b_mt_id) {
		write_lock(&b_mt_id->bm_lock);
		if (b_mt_id->bm_mdev) {
			dprintk("%s Removing DM device: %s %d:%d\n",
				__FUNCTION__,
				b_mt_id->bm_mdevname,
				MAJOR(b_mt_id->bm_mdev->bd_dev),
				MINOR(b_mt_id->bm_mdev->bd_dev));
			/* XXX Check status ?? */
			nfs4_blk_mdev_release(b_mt_id);
		}
		write_unlock(&b_mt_id->bm_lock);
		kfree(b_mt_id->bm_mdevname);
		kfree(b_mt_id);
	}
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
	struct nfs_server *server = NFS_SB(sb);
	LIST_HEAD(scsi_disklist);
	int status;

	dprintk("%s enter\n", __FUNCTION__);

	b_mt_id = kzalloc(sizeof(struct block_mount_id), GFP_KERNEL);
	if (!b_mt_id)
		goto out_error;
	/* Initialize nfs4 block layout mount id */
	b_mt_id->bm_sb = sb; /* back pointer to retrieve nfs_server struct */
	rwlock_init(&b_mt_id->bm_lock);
	/* 16 for 2 uint64_t, 2 for 2 ":" and 1 for the end zero */
	b_mt_id->bm_mdevname = kzalloc(strlen(server->nfs_client->cl_hostname)
				       + 16 + 2 + 1, GFP_KERNEL);
	if (!b_mt_id->bm_mdevname)
		goto out_error;
	sprintf(b_mt_id->bm_mdevname, "%s:%Lu:%Lu",
		server->nfs_client->cl_hostname,
		server->fsid.major, server->fsid.minor);
	dprintk("%s b_mt_id->bm_mdevname %s\n",
	       __FUNCTION__, b_mt_id->bm_mdevname);

	mtype = kzalloc(sizeof(struct pnfs_mount_type), GFP_KERNEL);
	if (!mtype)
		goto out_error;
	mtype->mountid = (void *)b_mt_id;

	/* Construct a list of all visible scsi disks that have not been
	 * claimed.
	 */
	status =  nfs4_blk_create_scsi_disk_list(sb, &scsi_disklist);
	if (status < 0)
		goto out_error;

	/* Retrieve device list from server. This returns the list in a
	 * per-layout type opaque buffer.
	 */
	dlist = kmalloc(sizeof(struct pnfs_devicelist), GFP_KERNEL);
	if (!dlist)
		goto out_error;
	status = pnfs_callback_ops->nfs_getdevicelist(sb, fh, dlist);
	if (status)
		goto out_error;

	/* Decode opaque devicelist, create a flat volume topology,
	 * matching VOLUME_SIMPLE disk signatures to disks in the
	 * visible scsi disk list. Construct an LVM meta device
	 * from the flat volume topology.
	 */
	status = nfs4_blk_process_devicelist(b_mt_id, dlist, &scsi_disklist);
	if (status)
		goto out_error;
	dprintk("%s SUCCESS\n", __FUNCTION__);

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

	dprintk("%s enter\n", __FUNCTION__);
	if (!mtype)
		return 0;
	b_mt_id = (struct block_mount_id *)mtype->mountid;
	free_blk_mountid(b_mt_id);
	kfree(mtype);
	dprintk("%s RETURNS\n", __FUNCTION__);
	return 0;
}

static ssize_t
bl_get_stripesize(struct pnfs_layout_type *layoutid)
{
	dprintk("%s enter\n", __FUNCTION__);
	return 0;
}

static ssize_t
bl_get_io_threshold(struct pnfs_layout_type *layoutid, struct inode *inode)
{
	dprintk("%s enter\n", __FUNCTION__);
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
	dprintk("%s enter\n", __FUNCTION__);
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
	dprintk("%s: NFSv4 Block Layout Driver Registering...\n", __FUNCTION__);

	pnfs_callback_ops = pnfs_register_layoutdriver(&blocklayout_type);
	return 0;
}

static void __exit nfs4blocklayout_exit(void)
{
	dprintk("%s: NFSv4 Block Layout Driver Unregistering...\n",
	       __FUNCTION__);

	pnfs_unregister_layoutdriver(&blocklayout_type);
}

module_init(nfs4blocklayout_init);
module_exit(nfs4blocklayout_exit);

