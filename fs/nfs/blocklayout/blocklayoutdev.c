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
