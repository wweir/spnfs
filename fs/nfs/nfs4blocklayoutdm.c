/*
 *  linux/fs/nfs/nfs4blocklayoutdm.c
 *
 *  Module for the NFSv4.1 pNFS block layout driver.
 *
 *  Copyright (c) 2007 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Fred Isaman <iisaman@umich.edu>
 *  Andy Adamson <andros@citi.umich.edu>
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

#include <linux/genhd.h> /* gendisk - used in a dprintk*/

#include "nfs4blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_BLOCKLAYOUT

/* Defines used for calculating memory usage in nfs4_blk_flatten() */
#define ARGSIZE   24    /* Max bytes needed for linear target arg string */
#define SPECSIZE (sizeof8(struct dm_target_spec) + ARGSIZE)
#define SPECS_PER_PAGE (PAGE_SIZE / SPECSIZE)
#define SPEC_HEADER_ADJUST (SPECS_PER_PAGE - \
			    (PAGE_SIZE - sizeof8(struct dm_ioctl)) / SPECSIZE)
#define roundup8(x) (((x)+7) & ~7)
#define sizeof8(x) roundup8(sizeof(x))

/* Given x>=1, return smallest n such that 2**n >= x */
static unsigned long find_order(int x)
{
	unsigned long rv = 0;
	for (x--; x; x >>= 1)
		rv++;
	return rv;
}

/* Debugging aid */
static void print_extent(u64 meta_offset, dev_t disk,
			 u64 disk_offset, u64 length)
{
	dprintk("%Li:, %d:%d %Li, %Li\n", meta_offset, MAJOR(disk),
			MINOR(disk), disk_offset, length);
}
static int dev_create(const char *name, dev_t *dev)
{
	struct dm_ioctl ctrl;
	int rv;

	memset(&ctrl, 0, sizeof(ctrl));
	strncpy(ctrl.name, name, DM_NAME_LEN-1);
	rv = dm_dev_create(&ctrl); /* XXX - need to pull data out of ctrl */
	dprintk("Tried to create %s, got %i\n", name, rv);
	if (!rv) {
		*dev = huge_decode_dev(ctrl.dev);
		dprintk("dev = (%i, %i)\n", MAJOR(*dev), MINOR(*dev));
	}
	return rv;
}

static int dev_remove(const char *name)
{
	struct dm_ioctl ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	strncpy(ctrl.name, name, DM_NAME_LEN-1);
	return dm_dev_remove(&ctrl);
}

static int dev_resume(const char *name)
{
	struct dm_ioctl ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	strncpy(ctrl.name, name, DM_NAME_LEN-1);
	return dm_do_resume(&ctrl);
}

/*
 * Release meta device
 */
int nfs4_blk_mdev_release(struct block_mount_id *b_mt_id)
{
	int rv;

	dprintk("%s Releasing %s\n", __FUNCTION__, b_mt_id->bm_mdevname);
	/* XXX Check return? */
	rv = nfs4_blkdev_put(b_mt_id->bm_mdev);
	dprintk("%s nfs4_blkdev_put returns %d\n", __FUNCTION__, rv);

	rv = dev_remove(b_mt_id->bm_mdevname);
	dprintk("%s Returns %d\n", __FUNCTION__, rv);
	return rv;
}

/*
 *  Create meta device. Keep it open to use for I/O.
 */
int nfs4_blk_init_mdev(struct block_mount_id *b_mt_id)
{
	struct block_device *bd;
	dev_t meta_dev;
	int rv;

	dprintk("%s for %s\n", __FUNCTION__, b_mt_id->bm_mdevname);

	rv = dev_create(b_mt_id->bm_mdevname, &meta_dev);
	if (rv)
		return rv;

	rv = -EIO;
	bd = nfs4_blkdev_get(meta_dev);
	if (!bd)
		goto out;

	if (bd_claim(bd, b_mt_id->bm_sb)) {
		dprintk("%s: failed to claim device %d:%d\n",
					__FUNCTION__,
					MAJOR(meta_dev),
					MINOR(meta_dev));
		blkdev_put(bd);
		goto out;
	}
	rv = 0;

	write_lock(&b_mt_id->bm_lock);
	b_mt_id->bm_mdev = bd;
	write_unlock(&b_mt_id->bm_lock);
	dprintk("%s Created device %s named %s with bd_block_size %u\n",
				__FUNCTION__,
				bd->bd_disk->disk_name,
				b_mt_id->bm_mdevname,
				bd->bd_block_size);
out:
	return rv;
}

/*
 * Given a vol_offset into root, returns the disk and disk_offset it
 * corresponds to, as well as the length of the contiguous segment thereafter.
 * All offsets/lengths are in 512-byte sectors.
 */
static int nfs4_blk_resolve(int root, struct pnfs_blk_volume *vols,
			    u64 vol_offset, dev_t *disk, u64 *disk_offset,
			    u64 *length)
{
	struct pnfs_blk_volume *node;
	u64 node_offset;

	/* Walk down device tree until we hit a leaf node (VOLUME_SIMPLE) */
	node = &vols[root];
	node_offset = vol_offset;
	*length = node->bv_size;
	while (1) {
		dprintk("node->bv_id = %i, offset=%Li, length=%Li\n",
			node->bv_id, node_offset, *length);
		if (node_offset > node->bv_size)
			return -EIO;
		switch (node->bv_type) {
		case VOLUME_SIMPLE:
			*disk = node->bv_dev;
			dprintk("%s VOLUME_SIMPLE: node->bv_dev %d:%d\n",
			       __FUNCTION__,
			       MAJOR(node->bv_dev),
			       MINOR(node->bv_dev));
			*disk_offset = node_offset;
			*length = min(*length, node->bv_size - node_offset);
			return 0;
		case VOLUME_SLICE:
			dprintk("%s VOLUME_SLICE:\n", __FUNCTION__);
			*length = min(*length, node->bv_size - node_offset);
			node_offset += node->bv_offset;
			node = node->bv_vols[0];
			break;
		case VOLUME_CONCAT: {
			u64 next = 0, sum = 0;
			int i;
			dprintk("%s VOLUME_CONCAT:\n", __FUNCTION__);
			for (i = 0; i < node->bv_vol_n; i++) {
				next = sum + node->bv_vols[i]->bv_size;
				if (node_offset < next)
					break;
				sum = next;
			}
			*length = min(*length, next - node_offset);
			node_offset -= sum;
			node = node->bv_vols[i];
			}
			break;
		case VOLUME_STRIPE: {
			u64 global_s_no;
			u64 stripe_pos;
			u64 local_s_no;
			u64 disk_number;

			dprintk("%s VOLUME_STRIPE:\n", __FUNCTION__);
			global_s_no = node_offset;
			/* BUG - note this assumes stripe_unit <= 2**32 */
			stripe_pos = (u64) do_div(global_s_no,
						  (u32)node->bv_stripe_unit);
			local_s_no = global_s_no;
			disk_number = (u64) do_div(local_s_no,
						   (u32) node->bv_vol_n);
			*length = min(*length,
				      node->bv_stripe_unit - stripe_pos);
			node_offset = local_s_no * node->bv_stripe_unit +
					stripe_pos;
			node = node->bv_vols[disk_number];
			}
			break;
		default:
			return -EIO;
		}
	}
}

/*
 * Create an LVM dm device table that represents the volume topology returned
 * by GETDEVICELIST or GETDEVICEINFO.
 *
 * vols:  topology with VOLUME_SIMPLEs mapped to visable scsi disks.
 * size:  number of volumes in vols.
 */
int nfs4_blk_flatten(struct pnfs_blk_volume *vols, int size,
		     struct block_mount_id *b_mt_id)
{
	u64 meta_offset = 0;
	u64 meta_size = vols[size-1].bv_size;
	dev_t disk;
	u64 disk_offset, len;
	int status = 0, count = 0, pages_needed;
	struct dm_ioctl *ctl;
	struct dm_target_spec *spec;
	char *args = NULL;
	unsigned long p;

	dprintk("%s enter. mdevname %s number of volumes %d\n", __FUNCTION__,
			b_mt_id->bm_mdevname, size);

	/* We need to reserve memory to store segments, so need to count
	 * segments.  This means we resolve twice, basically throwing away
	 * all info from first run apart from the count.  Seems like
	 * there should be a better way.
	 */
	for (meta_offset = 0; meta_offset < meta_size; meta_offset += len) {
		status = nfs4_blk_resolve(size-1, vols, meta_offset, &disk,
						&disk_offset, &len);
		/* TODO Check status */
		count += 1;
	}

	dprintk("%s: Have %i segments\n", __FUNCTION__, count);
	pages_needed = ((count + SPEC_HEADER_ADJUST) / SPECS_PER_PAGE) + 1;
	dprintk("%s: Need %i pages\n", __FUNCTION__, pages_needed);
	p = __get_free_pages(GFP_KERNEL, find_order(pages_needed));
	if (!p)
		return -ENOMEM;
	/* A dm_ioctl is placed at the beginning, followed by a series of
	 * (dm_target_spec, argument string) pairs.
	 */
	ctl = (struct dm_ioctl *) p;
	spec = (struct dm_target_spec *) (p + sizeof8(*ctl));
	memset(ctl, 0, sizeof(*ctl));
	ctl->data_start = (char *) spec - (char *) ctl;
	ctl->target_count = count;
	strncpy(ctl->name, b_mt_id->bm_mdevname, DM_NAME_LEN);

	dprintk("%s ctl->name %s\n", __FUNCTION__, ctl->name);
	for (meta_offset = 0; meta_offset < meta_size; meta_offset += len) {
		status = nfs4_blk_resolve(size-1, vols, meta_offset, &disk,
							&disk_offset, &len);
		if (!len)
			break;
		/* TODO Check status */
		print_extent(meta_offset, disk, disk_offset, len);
		spec->sector_start = meta_offset;
		spec->length = len;
		spec->status = 0;
		strcpy(spec->target_type, "linear");
		args = (char *) (spec + 1);
		sprintf(args, "%i:%i %Li",
			MAJOR(disk), MINOR(disk), disk_offset);
		dprintk("%s args %s\n", __FUNCTION__, args);
		spec->next = roundup8(sizeof(*spec) + strlen(args) + 1);
		spec = (struct dm_target_spec *) (((char *) spec) + spec->next);
	}
	ctl->data_size = (char *) spec - (char *) ctl;

	status = dm_table_load(ctl, ctl->data_size);
	dprintk("%s dm_table_load returns %d\n", __FUNCTION__, status);

	dev_resume(b_mt_id->bm_mdevname);

	free_pages(p, find_order(pages_needed));
	dprintk("%s returns %d\n", __FUNCTION__, status);
	return status;
}
