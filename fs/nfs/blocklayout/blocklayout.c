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

#include <linux/buffer_head.h> /* DEBUG Needed for print_page calls */
#include <linux/bio.h> /* struct bio */
#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

/* Callback operations to the pNFS client */
struct pnfs_client_operations *pnfs_callback_ops;

/* Right now, these are basically just a bit that is passed from
 * nfs_write_begin to nfs_write_end, which I've done like so to avoid
 * having to keep malloc/free'ing fsdata.
 * XXX In particular, currently assumes only one mountpoint is in use.
 */
static struct pnfs_fsdata *bl_use_pnfs;
static struct pnfs_fsdata *bl_use_mds;

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
		dprintk("        be_f_offset %Lu\n", (u64)be->be_f_offset);
		dprintk("        be_length   %Lu\n", (u64)be->be_length);
		dprintk("        be_v_offset %Lu\n", (u64)be->be_v_offset);
		dprintk("        be_state    %d\n", be->be_state);
	}
}

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

/* Returns extent, or NULL.  If a second READ extent exists, it is returned
 * in cow_read, if given.
 *
 * We assume about the extent list:
 * 1. Extents are ordered by file offset, if two extents have same offset,
 *    we don't care about ordering.
 * 2. For any given isect, there are at most two extents that match.
 * 3. If two extents match, exactly one will have state==READ_DATA
 */
static struct pnfs_block_extent *
find_get_extent(struct pnfs_layout_segment *lseg, sector_t isect,
	    struct pnfs_block_extent **cow_read)
{
	struct pnfs_block_layout *bl = BLK_LO(lseg);
	struct pnfs_block_extent *be, *cow, *out;

	dprintk("%s enter with isect %Lu\n", __func__, (u64)isect);
	cow = out = NULL;
	spin_lock(&bl->bl_ext_lock);
	list_for_each_entry(be, &bl->bl_extents, be_node) {
		if (isect < be->be_f_offset)
			break;
		if (isect < be->be_f_offset + be->be_length) {
			/* We have found an extent, now decide if it should
			 * be returned in cow_read or not.
			 */
			dprintk("%s Get %p (%i)\n", __func__, be,
				atomic_read(&be->be_refcnt.refcount));
			kref_get(&be->be_refcnt);
			if (!out)
				out = be;
			else {
				if (out->be_state == PNFS_BLOCK_READ_DATA) {
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
	if (be->be_state == PNFS_BLOCK_INVALID_DATA ||
	    be->be_state == PNFS_BLOCK_NONE_DATA)
		return 1;
	else if (be->be_state != PNFS_BLOCK_NEEDS_INIT)
		return 0;
	else {
		uint32_t mask;
		mask = 1 << ((isect - be->be_f_offset) >>
			     (PAGE_CACHE_SHIFT - 9));
		return be->be_bitmap & mask;
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

/* Copy data from old to new, remove old from list and add new in its place. */
static void
clone_extent(struct pnfs_block_extent *old, struct pnfs_block_extent *new)
{
	memcpy(new, old, sizeof(*new));
	INIT_LIST_HEAD(&new->be_node);
	kref_init(&new->be_refcnt);
	list_replace(&old->be_node, &new->be_node);
	put_extent(old);
}

/*
 *      |-------------old-----------------|
 *      |----len----|
 *
 * 		becomes
 *
 *      |----new----|-----------old-------|
 *
*/
static void
split_extent_helper(struct pnfs_block_layout *bl,
		    struct pnfs_block_extent *old, sector_t len,
		    struct pnfs_block_extent *new)
{
	struct pnfs_block_extent *next;

	INIT_LIST_HEAD(&new->be_node);
	kref_init(&new->be_refcnt);
	new->be_bitmap = 0;
	new->be_f_offset = old->be_f_offset;
	new->be_length = len;
	new->be_v_offset = old->be_v_offset;
	new->be_state = old->be_state;
	old->be_f_offset += len;
	old->be_length -= len;
	old->be_v_offset += len;
	/* Because list is sorted by offset, new will be in old's place */
	list_replace(&old->be_node, &new->be_node);
	/* However, old is not necessarily next.  A cow READ extent may
	 * intervene.
	 */
	next = list_prepare_entry(new, &bl->bl_extents, be_node);
	list_for_each_entry_continue(next, &bl->bl_extents, be_node) {
		if (next->be_f_offset > old->be_f_offset)
			break;
	}
	list_add_tail(&old->be_node, &next->be_node);
	bl->bl_n_ext++;
}

/*
 * Finds the extent containing isect, and if it is INVAL, splits it out
 * so isect is in a NEEDS_INIT extent.
 */
static struct pnfs_block_extent *
split_inval_extent(struct pnfs_layout_segment *lseg,
		   struct pnfs_block_extent *be,
		   sector_t isect, sector_t len)
{
	struct pnfs_block_layout *bl = BLK_LO(lseg);
	struct pnfs_block_extent *rv = NULL, *inval = NULL, *e1, *e2, *e3;
	struct nfs_server *nfss = PNFS_NFS_SERVER(lseg->layout);

	dprintk("%s isect=%Lu\n", __func__, (u64)isect);
	put_extent(be);

	/* Preallocate prior to taking spinlock */
	e1 = e2 = e3 = NULL;
	e1 = kmalloc(sizeof(*e1), GFP_KERNEL);
	e2 = kmalloc(sizeof(*e2), GFP_KERNEL);
	e3 = kmalloc(sizeof(*e3), GFP_KERNEL);
	if (!e1 || !e2 || !e3) {
		kfree(e1);
		kfree(e2);
		kfree(e3);
		return NULL;
	}

	spin_lock(&bl->bl_ext_lock);
	/* Find the INVAL extent to split */
	list_for_each_entry(be, &bl->bl_extents, be_node) {
		if (isect < be->be_f_offset)
			break;
		if (isect < be->be_f_offset + be->be_length) {
			if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
				inval = be;
				break;
			} else if (be->be_state == PNFS_BLOCK_READWRITE_DATA ||
				   be->be_state == PNFS_BLOCK_NEEDS_INIT)
				rv = be;
		}
	}
	if (!inval) {
		/* Hopefully, this is due to someone else doing the split */
		dprintk("%s Could not find INVAL to split\n", __func__);
		kfree(e1);
		kfree(e2);
		kfree(e3);
		goto out;
	}
	/* Shift to block boundaries */
	if (nfss->pnfs_blksize) {
		u32 mask = (nfss->pnfs_blksize >> 9) - 1;
		isect &= ~mask;
		len = (len + mask) & ~mask;
		/* We can only hold 32 pages in a NEEDS_INIT extent */
		len = min(len, (sector_t) (32 << (PAGE_CACHE_SHIFT - 9)));
	}

	/* Split inval */
	clone_extent(inval, e2);
	rv = e2;
	inval = NULL;   /* not needed, but helps prevent errors */
	if (e2->be_f_offset != isect)
		split_extent_helper(bl, e2, isect - e2->be_f_offset, e1);
	else
		kfree(e1);
	if (e2->be_length > len) {
		split_extent_helper(bl, e2, len, e3);
		rv = e3;
	} else {
		kfree(e3);
		if (e2->be_length < len) {
			/* This can't happen now, while len == blksize, but
			 * if we choose to use larger lengths, must take
			 * care of this.
			 */
			dprintk("%s BUG - len too large\n", __func__);
		}
	}
	rv->be_state = PNFS_BLOCK_NEEDS_INIT;
	rv->be_bitmap = (1 << (len >> (PAGE_CACHE_SHIFT - 9))) - 1;
 out:
	spin_unlock(&bl->bl_ext_lock);
	if (rv)
		kref_get(&rv->be_refcnt);
	return rv;
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

	dprintk("%s called with err=%i\n", __func__, err);
	bl_readlist_done(data, err);
	bio_put(bio);
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

	dprintk("%s enter nr_pages %u offset %Ld count %Zd\n", __func__,
	       nr_pages, f_offset, count);

	if (f_offset & 0x1ff) {
		/* This shouldn't be needed, just being paranoid */
		int diff;
		dprintk("%s f_offset %Ld not aligned\n",
			__func__, f_offset);
		diff = f_offset & 0x1ff;
		f_offset &= ~0x1ff;
		count += diff;
	}
	if (dont_like_caller(rdata->req)) {
		dprintk("%s dont_like_caller failed\n", __func__);
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
		dprintk("%s Zeroing pages for hole\n", __func__);
		for (i = 0; i < nr_pages; i++) {
			zero_user(pages[i], 0,
				  min_t(int, PAGE_CACHE_SIZE, count));
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
		bio->bi_bdev = be_read->be_mdev;
		bio->bi_end_io = bl_end_read_bio;
		bio->bi_private = rdata;
		for (i = 0; i < nr_pages; i++) {
			added = bio_add_page(bio, pages[i], PAGE_SIZE, 0);
			if (added < PAGE_SIZE) {
				dprintk("%s bio_add_page(%lu)=%i\n",
					__func__, PAGE_SIZE, added);
				bio_put(bio);
				goto use_mds;
			}
		}
		dprintk("%s submitting read bio %u@%Lu\n", __func__,
			bio->bi_size, (u64)bio->bi_sector);
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

	if (!bl_use_pnfs) {
		bl_use_pnfs = kzalloc(sizeof(struct pnfs_fsdata), GFP_KERNEL);
		if (!bl_use_pnfs)
			goto out_error;
	}
	if (!bl_use_mds) {
		bl_use_mds = kzalloc(sizeof(struct pnfs_fsdata), GFP_KERNEL);
		if (!bl_use_mds)
			goto out_error;
	}
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
	kfree(bl_use_pnfs);
	kfree(bl_use_mds);
	bl_use_pnfs = bl_use_mds = NULL;
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
	kfree(bl_use_pnfs);
	kfree(bl_use_mds);
	bl_use_pnfs = bl_use_mds = NULL;
	if (!mtype)
		return 0;
	b_mt_id = (struct block_mount_id *)mtype->mountid;
	free_blk_mountid(b_mt_id);
	kfree(mtype);
	dprintk("%s RETURNS\n", __func__);
	return 0;
}

/* STUB - mark intersection of layout and page as bad, so is not
 * used again.
 */
static void mark_bad_read(void)
{
	return;
}

/* Copied from buffer.c */
static void __end_buffer_read_notouch(struct buffer_head *bh, int uptodate)
{
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		/* This happens, due to failed READA attempts. */
		clear_buffer_uptodate(bh);
	}
	unlock_buffer(bh);
}

/* Copied from buffer.c */
static void end_buffer_read_nobh(struct buffer_head *bh, int uptodate)
{
	__end_buffer_read_notouch(bh, uptodate);
}

/*
 * map_block:  map a requested I/0 block (isect) into an offset in the LVM
 * meta block_device
 */
static void
map_block(sector_t isect, struct pnfs_block_extent *be,
	  struct buffer_head *res_bh, unsigned int bitsize)
{
	dprintk("%s enter be=%p\n", __func__, be);

	set_buffer_mapped(res_bh);
	res_bh->b_bdev = be->be_mdev;
	res_bh->b_blocknr = (isect - be->be_f_offset + be->be_v_offset) >>
				bitsize;
	res_bh->b_size = 1 << (bitsize + 9);

	dprintk("%s isect %ld, res_bh->b_blocknr %ld, using bsize %Zd\n",
				__func__, (long)isect,
				(long)res_bh->b_blocknr,
				res_bh->b_size);
	return;
}

/* This is loosely based on nobh_write_begin */
static int
bl_write_begin(struct pnfs_layout_segment *lseg, struct page *page, loff_t pos,
	       unsigned count, struct pnfs_fsdata **fsdata)
{
	struct buffer_head *bh;
	unsigned from, to;
	int inval, ret = 0;
	struct pnfs_block_extent *be = NULL, *cow_read = NULL;
	sector_t isect;

	dprintk("%s enter, %u@%Ld\n", __func__, count, pos);
	print_page(page);
	/* The following code assumes blocksize == PAGE_CACHE_SIZE */
	if (PNFS_INODE(lseg->layout)->i_blkbits != PAGE_CACHE_SHIFT) {
		dprintk("%s Can't handle blocksize\n", __func__);
		*fsdata = bl_use_mds;
		return 0;
	}
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + count;
	*fsdata = bl_use_pnfs;

	if (PageMappedToDisk(page)) {
		/* Basically, this is a flag that says we have
		 * successfully called write_begin already on this page.
		 */
		return 0;
	}

	bh = alloc_page_buffers(page, PAGE_CACHE_SIZE, 0);
	if (!bh) {
		ret = -ENOMEM;
		goto cleanup;
	}

	isect = (sector_t)page->index << (PAGE_CACHE_SHIFT - 9);
	be = find_get_extent(lseg, isect, &cow_read);
	if (!be) {
		*fsdata = bl_use_mds;
		goto cleanup;
	}
	inval = is_hole(be, isect);
	dprintk("%s inval=%i, from=%u, to=%u\n", __func__, inval, from, to);
	if (inval) {
		if (be->be_state == PNFS_BLOCK_NONE_DATA) {
			dprintk("%s PANIC - got NONE_DATA extent %p\n",
				__func__, be);
			*fsdata = bl_use_mds;
			goto cleanup;
		}
		map_block(isect, be, bh, PAGE_CACHE_SHIFT - 9);
		/* FRED - I don't understand this, not sure is needed */
		unmap_underlying_metadata(bh->b_bdev, bh->b_blocknr);
	}
	if (PageUptodate(page)) {
		/* Do nothing */
	} else if (inval & !cow_read) {
		char *kaddr = kmap_atomic(page, KM_USER0);
		if (0 < from) {
			dprintk("%s memset(0 -> %u)\n", __func__, from);
			memset(kaddr, 0, from);
		}
		if (PAGE_CACHE_SIZE > to) {
			dprintk("%s memset(%u -> %lu)\n", __func__,
				to, PAGE_CACHE_SIZE);
			memset(kaddr + to, 0, PAGE_CACHE_SIZE - to);
		}
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
	} else if (0 < from || PAGE_CACHE_SIZE > to) {
		struct pnfs_block_extent *read_extent;

		read_extent = (inval && cow_read) ? cow_read : be;
		map_block(isect, read_extent, bh, PAGE_CACHE_SHIFT - 9);
		lock_buffer(bh);
		bh->b_end_io = end_buffer_read_nobh;
		submit_bh(READ, bh);
		dprintk("%s: Waiting for buffer read\n", __func__);
		/* XXX Don't really want to hold layout lock here */
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			*fsdata = bl_use_mds;
			ret = -EIO;
		}
		if (ret)
			goto cleanup;
	}
	if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
		/* NOTE be changes, normally new be_state will be NEEDS_INIT */
		be = split_inval_extent(lseg, be, isect, PAGE_CACHE_SIZE >> 9);
		if (!be) {
			dprintk("%s split failed\n", __func__);
			*fsdata = bl_use_mds;
			goto cleanup;
		}
		/* STUB - need to mark adjacent pages in large block */
	}
	if (be->be_state == PNFS_BLOCK_NEEDS_INIT) {
		uint32_t mask = 1 << ((isect - be->be_f_offset) >>
				      (PAGE_CACHE_SHIFT - 9));
		be->be_bitmap &= ~mask;
	}
	SetPageMappedToDisk(page);
	ret = 0; /* Not technically needed */

cleanup:
	dprintk("%s cleanup, ret=%i\n", __func__, ret);
	free_buffer_head(bh);
	put_extent(be);
	put_extent(cow_read);
	if (ret) {
		/* Need to mark layout with bad read...should now
		 * just use nfs4 for reads and writes.
		 */
		mark_bad_read();
		/* Revert back to plain NFS and just continue on with
		 * write.  This assumes there is no request attached, which
		 * should be true if we get here.
		 */
		BUG_ON(PagePrivate(page));
		*fsdata = bl_use_mds;
		ret = 0;
	}
	return ret;
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

static int
bl_do_flush(struct pnfs_layout_segment *lseg, struct nfs_page *req,
	    struct pnfs_fsdata *fsdata)
{
	dprintk("%s enter\n", __func__);
	/* This checks if old req will likely use same io method as soon
	 * to be created request, and returns False if they are the same.
	 */
	return (!lseg != !test_bit(PG_USE_PNFS, &req->wb_flags));
}

static struct layoutdriver_io_operations blocklayout_io_operations = {
	.commit				= bl_commit,
	.read_pagelist			= bl_read_pagelist,
	.write_pagelist			= bl_write_pagelist,
	.write_begin			= bl_write_begin,
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
	.do_flush			= bl_do_flush,
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
