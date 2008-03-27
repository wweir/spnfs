/*
 *  linux/fs/nfs/blocklayout/blocklayoutdev.c
 *
 *  Device operations for the pnfs nfs4 file layout driver.
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
#include <linux/buffer_head.h> /* __bread */

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

#define MAX_VOLS  256  /* Maximum number of SCSI disks.  Totally arbitrary */

uint32_t *blk_overflow(uint32_t *p, uint32_t *end, size_t nbytes)
{
	uint32_t *q = p + XDR_QUADLEN(nbytes);
	if (unlikely(q > end || q < p))
		return NULL;
	return p;
}
EXPORT_SYMBOL(blk_overflow);

/* Open a block_device by device number. */
static struct block_device *nfs4_blkdev_get(dev_t dev)
{
	struct block_device *bd;

	dprintk("%s enter\n", __func__);
	bd = open_by_devnum(dev, FMODE_READ);
	if (IS_ERR(bd))
		goto fail;
	return bd;
fail:
	dprintk("%s failed to open device : %ld\n",
			__func__, PTR_ERR(bd));
	return NULL;
}

/*
 * Release the block device
 */
static int nfs4_blkdev_put(struct block_device *bdev)
{
	dprintk("%s for device %d:%d\n", __func__, MAJOR(bdev->bd_dev),
			MINOR(bdev->bd_dev));
	bd_release(bdev);
	return blkdev_put(bdev);
}

/* Add a visible, claimed (by us!) scsi disk to the device list */
static int alloc_add_disk(struct block_device *blk_dev, struct list_head *dlist)
{
	struct visible_block_device *vis_dev;

	dprintk("%s enter\n", __func__);
	vis_dev = kmalloc(sizeof(struct visible_block_device), GFP_KERNEL);
	if (!vis_dev) {
		dprintk("%s nfs4_get_sig failed\n", __func__);
		return -ENOMEM;
	}
	vis_dev->vi_bdev = blk_dev;
	vis_dev->vi_mapped = 0;
	list_add(&vis_dev->vi_node, dlist);
	return 0;
}

/* Walk the list of scsi_devices. Add disks that can be opened and claimed
 * to the device list
 */
static int
nfs4_blk_add_scsi_disk(struct Scsi_Host *shost,
		       int index, struct list_head *dlist)
{
	static char *claim_ptr = "I belong to pnfs block driver";
	struct block_device *bdev;
	struct gendisk *gd;
	struct scsi_device *sdev;
	unsigned int major, minor, ret = 0;
	dev_t dev;

	dprintk("%s enter \n", __func__);
	if (index >= MAX_VOLS) {
		dprintk("%s MAX_VOLS hit\n", __func__);
		return -ENOSPC;
	}
	dprintk("%s 1 \n", __func__);
	index--;
	shost_for_each_device(sdev, shost) {
		dprintk("%s 2\n", __func__);
		/* Need to do this check before bumping index */
		if (sdev->type != TYPE_DISK)
			continue;
		dprintk("%s 3 index %d \n", __func__, index);
		if (++index >= MAX_VOLS) {
			scsi_device_put(sdev);
			break;
		}
		major = (!(index >> 4) ? SCSI_DISK0_MAJOR :
			 SCSI_DISK1_MAJOR-1 + (index  >> 4));
		minor =  ((index << 4) & 255);

		dprintk("%s SCSI device %d:%d \n", __func__, major, minor);

		dev = MKDEV(major, minor);
		bdev = nfs4_blkdev_get(dev);
		if (!bdev) {
			dprintk("%s: failed to open device %d:%d\n",
					__func__, major, minor);
			continue;
		}
		gd = bdev->bd_disk;

		dprintk("%s 4\n", __func__);

		if (bd_claim(bdev, claim_ptr)) {
			dprintk("%s: failed to claim device %d:%d\n",
				__func__, gd->major, gd->first_minor);
			blkdev_put(bdev);
			continue;
		}

		ret = alloc_add_disk(bdev, dlist);
		if (ret < 0)
			goto out_err;
		dprintk("%s ADDED DEVICE capacity %ld, bd_block_size %d\n",
					__func__,
					(unsigned long)gd->capacity,
					bdev->bd_block_size);

	}
	index++;
	dprintk("%s returns index %d \n", __func__, index);
	return index;

out_err:
	dprintk("%s Can't add disk to list. ERROR: %d\n", __func__, ret);
	nfs4_blkdev_put(bdev);
	return ret;
}

/* Destroy the temporary scsi disk list */
void nfs4_blk_destroy_disk_list(struct list_head *dlist)
{
	struct visible_block_device *vis_dev;

	dprintk("%s enter\n", __func__);
	while (!list_empty(dlist)) {
		vis_dev = list_first_entry(dlist, struct visible_block_device,
					   vi_node);
		dprintk("%s removing device %d:%d\n", __func__,
				MAJOR(vis_dev->vi_bdev->bd_dev),
				MINOR(vis_dev->vi_bdev->bd_dev));
		list_del(&vis_dev->vi_node);
		if (!vis_dev->vi_mapped)
			nfs4_blkdev_put(vis_dev->vi_bdev);
		kfree(vis_dev);
	}
}

/*
 * Create a temporary list of all SCSI disks host can see, and that have not
 * yet been claimed.
 * shost_class: list of all registered scsi_hosts
 * returns -errno on error, and #of devices found on success.
 * XXX Loosely emulate scsi_host_lookup from scsi/host.c
*/
int nfs4_blk_create_scsi_disk_list(struct list_head *dlist)
{
	struct class *class = &shost_class;
	struct class_device *cdev;
	struct Scsi_Host *shost;
	int ret = 0, index = 0;

	dprintk("%s enter\n", __func__);

	down(&class->sem);
	list_for_each_entry(cdev, &class->children, node) {
		dprintk("%s 1\n", __func__);
		shost = class_to_shost(cdev);
		ret = nfs4_blk_add_scsi_disk(shost, index, dlist);
		dprintk("%s 2 ret %d\n", __func__, ret);
		if (ret < 0)
			goto out;
		index = ret;
	}
out:
	up(&class->sem);
	return ret;
}
/* We are given an array of XDR encoded deviceid4's, each of which should
 * refer to a previously decoded device.  Translate into a list of pointers
 * to the appropriate pnfs_blk_volume's.
 */
static int set_vol_array(uint32_t **pp, uint32_t *end,
			 struct pnfs_blk_volume *vols, int working)
{
	struct pnfs_deviceid id;
	int i, j;
	uint32_t *p = *pp;
	struct pnfs_blk_volume **array = vols[working].bv_vols;
	for (i = 0; i < vols[working].bv_vol_n; i++) {
		BLK_READBUF(p, end, NFS4_PNFS_DEVICEID4_SIZE);
		READ_DEVID(&id);
		/* Need to convert id into index */
		for (j = 0; j < working; j++) {
			if (memcmp(vols[j].bv_id.data, id.data,
				   NFS4_PNFS_DEVICEID4_SIZE) == 0)
				break;
		}
		if (j == working) {
			/* Could not find device id */
			dprintk("Could not find referenced deviceid4");
 out_err:
			return -EIO;
		}
		array[i] = &vols[j];
	}
	*pp = p;
	return 0;
}

static uint64_t sum_subvolume_sizes(struct pnfs_blk_volume *vol)
{
	int i;
	uint64_t sum = 0;
	for (i = 0; i < vol->bv_vol_n; i++)
		sum += vol->bv_vols[i]->bv_size;
	return sum;
}

static int decode_blk_signature(uint32_t **pp, uint32_t *end,
				struct pnfs_blk_sig *sig)
{
	int i, tmp;
	uint32_t *p = *pp;

	BLK_READBUF(p, end, 4);
	READ32(sig->si_num_comps);
	if (sig->si_num_comps >= PNFS_BLOCK_MAX_SIG_COMP) {
		dprintk("number of sig comps %i >= PNFS_BLOCK_MAX_SIG_COMP\n",
		       sig->si_num_comps);
		goto out_err;
	}
	for (i = 0; i < sig->si_num_comps; i++) {
		BLK_READBUF(p, end, 12);
		READ64(sig->si_comps[i].bs_offset);
		READ32(tmp);
		sig->si_comps[i].bs_length = tmp;
		BLK_READBUF(p, end, tmp);
		sig->si_comps[i].bs_string = (char *)p;
		p += XDR_QUADLEN(tmp);
	}
	*pp = p;
	return 0;
 out_err:
	return -EIO;
}

/* Translate a signature component into a block and offset. */
static void get_sector(struct block_device *bdev,
		       struct pnfs_blk_sig_comp *comp,
		       sector_t *block,
		       uint32_t *offset_in_block)
{
	int64_t use_offset = comp->bs_offset;
	unsigned int blkshift = blksize_bits(block_size(bdev));

	dprintk("%s enter\n", __func__);
	if (use_offset < 0)
		use_offset += (bdev->bd_disk->capacity << 9);
	*block = use_offset >> blkshift;
	*offset_in_block = use_offset - (*block << blkshift);

	dprintk("%s block %Lu offset_in_block %u\n",
			__func__, (u64)*block, *offset_in_block);
	return;
}

/*
 * All signatures in sig must be found on bdev for verification.
 * Returns True if sig matches, False otherwise.
 *
 * STUB - signature crossing a block boundary will cause problems.
 */
static int verify_sig(struct block_device *bdev, struct pnfs_blk_sig *sig)
{
	sector_t block = 0;
	struct pnfs_blk_sig_comp *comp;
	struct buffer_head *bh = NULL;
	uint32_t offset_in_block = 0;
	char *ptr;
	int i;

	dprintk("%s enter. bd_disk->capacity %ld, bd_block_size %d\n",
			__func__, (unsigned long)bdev->bd_disk->capacity,
			bdev->bd_block_size);
	for (i = 0; i < sig->si_num_comps; i++) {
		comp = &sig->si_comps[i];
		dprintk("%s comp->bs_offset %Ld, length=%d\n", __func__,
			comp->bs_offset, comp->bs_length);
		get_sector(bdev, comp, &block, &offset_in_block);
		bh = __bread(bdev, block, bdev->bd_block_size);
		if (!bh)
			goto out_err;
		ptr = (char *)bh->b_data + offset_in_block;
		if (memcmp(ptr, comp->bs_string, comp->bs_length))
			goto out_err;
		brelse(bh);
	}
	dprintk("%s Complete Match Found\n", __func__);
	return 1;

out_err:
	brelse(bh);
	dprintk("%s  No Match\n", __func__);
	return 0;
}

/*
 * map_sig_to_device()
 * Given a signature, walk the list of visible scsi disks searching for
 * a match. Returns True if mapping was done, False otherwise.
 *
 * While we're at it, fill in the vol->bv_size.
 */
/* XXX FRED - use normal 0=success status */
static int map_sig_to_device(struct pnfs_blk_sig *sig,
			     struct pnfs_blk_volume *vol,
			     struct list_head *sdlist)
{
	int mapped = 0;
	struct visible_block_device *vis_dev;

	list_for_each_entry(vis_dev, sdlist, vi_node) {
		if (vis_dev->vi_mapped)
			continue;
		mapped = verify_sig(vis_dev->vi_bdev, sig);
		if (mapped) {
			vol->bv_dev = vis_dev->vi_bdev->bd_dev;
			vol->bv_size = vis_dev->vi_bdev->bd_disk->capacity;
			vis_dev->vi_mapped = 1;
			/* XXX FRED check this */
			/* We no longer need to scan this device, and
			 * we need to "put" it before creating metadevice.
			 */
			nfs4_blkdev_put(vis_dev->vi_bdev);
			break;
		}
	}
	return mapped;
}

/* XDR decodes pnfs_block_volume4 structure */
static int decode_blk_volume(uint32_t **pp, uint32_t *end,
			     struct pnfs_blk_volume *vols, int i,
			     struct list_head *sdlist, int *array_cnt)
{
	int status = 0;
	struct pnfs_blk_sig sig;
	uint32_t *p = *pp;
	uint64_t tmp; /* Used by READ_SECTOR */
	struct pnfs_blk_volume *vol = &vols[i];

	BLK_READBUF(p, end, 4);
	READ32(vol->bv_type);
	dprintk("%s vol->bv_type = %i\n", __func__, vol->bv_type);
	BLK_READBUF(p, end, NFS4_PNFS_DEVICEID4_SIZE);
	READ_DEVID(&(vol->bv_id));
	/* dprintk("%s vol->bv_id = %i\n", __func__, vol->bv_id); */
	switch (vol->bv_type) {
	case PNFS_BLOCK_VOLUME_SIMPLE:
		*array_cnt = 0;
		status = decode_blk_signature(&p, end, &sig);
		if (status)
			return status;
		status = map_sig_to_device(&sig, vol, sdlist);
		if (!status) {
			dprintk("Could not find disk for device\n");
			return -EIO;
		}
		status = 0;
		dprintk("%s Set Simple vol to dev %d:%d, size %Lu\n",
				__func__,
				MAJOR(vol->bv_dev),
				MINOR(vol->bv_dev),
				(u64)vol->bv_size);
		break;
	case PNFS_BLOCK_VOLUME_SLICE:
		BLK_READBUF(p, end, 16);
		READ_SECTOR(vol->bv_offset);
		READ_SECTOR(vol->bv_size);
		*array_cnt = vol->bv_vol_n = 1;
		status = set_vol_array(&p, end, vols, i);
		break;
	case PNFS_BLOCK_VOLUME_STRIPE:
		BLK_READBUF(p, end, 8);
		READ_SECTOR(vol->bv_stripe_unit);
		/* Fall through */
	case PNFS_BLOCK_VOLUME_CONCAT:
		BLK_READBUF(p, end, 4);
		READ32(vol->bv_vol_n);
		if (!vol->bv_vol_n)
			return -EIO;
		*array_cnt = vol->bv_vol_n;
		status = set_vol_array(&p, end, vols, i);
		vol->bv_size = sum_subvolume_sizes(vol);
		dprintk("%s Set Concat vol to size %Lu\n",
				__func__, (u64)vol->bv_size);
		break;
	default:
		dprintk("Unknown volume type %i\n", vol->bv_type);
 out_err:
		return -EIO;
	}
	*pp = p;
	return status;
}

/* Decodes pnfs_block_deviceaddr4 (draft-5) which is XDR encoded
 * in dev->dev_addr_buf.
 */
struct pnfs_block_dev *
nfs4_blk_decode_device(struct super_block *sb,
				  struct pnfs_device *dev,
				  struct list_head *sdlist)
{
	int num_vols, i, status, count;
	struct pnfs_blk_volume *vols, **arrays, **arrays_ptr;
	uint32_t *p = (uint32_t *)dev->dev_addr_buf;
	uint32_t *end = (uint32_t *) ((char *) p + dev->dev_addr_len);
	struct pnfs_block_dev *rv = NULL;
	struct visible_block_device *vis_dev;

	dprintk("%s enter\n", __func__);

	READ32(num_vols);
	dprintk("%s num_vols = %i\n", __func__, num_vols);

	vols = kmalloc(sizeof(struct pnfs_blk_volume) * num_vols, GFP_KERNEL);
	if (!vols)
		return NULL;
	/* Each volume in vols array needs its own array.  Save time by
	 * allocating them all in one large hunk.  Because each volume
	 * array can only reference previous volumes, and because once
	 * a concat or stripe references a volume, it may never be
	 * referenced again, the volume arrays are guaranteed to fit
	 * in the suprisingly small space allocated.
	 */
	arrays = kmalloc(sizeof(struct pnfs_blk_volume *) * num_vols * 2,
			 GFP_KERNEL);
	if (!arrays)
		goto out;
	arrays_ptr = arrays;

	list_for_each_entry(vis_dev, sdlist, vi_node) {
		/* Wipe crud left from parsing previous device */
		vis_dev->vi_mapped = 0;
	}
	for (i = 0; i < num_vols; i++) {
		vols[i].bv_vols = arrays_ptr;
		status = decode_blk_volume(&p, end, vols, i, sdlist, &count);
		if (status)
			goto out;
		arrays_ptr += count;
	}

	/* Check that we have used up opaque */
	if (p != end) {
		dprintk("Undecoded cruft at end of opaque\n");
		goto out;
	}

	/* Now use info in vols to create the meta device */
	rv = nfs4_blk_init_metadev(sb, dev);
	if (!rv)
		goto out;
	status = nfs4_blk_flatten(vols, num_vols, rv);
	if (status) {
		free_block_dev(rv);
		rv = NULL;
	}
 out:
	kfree(arrays);
	kfree(vols);
	return rv;
}
