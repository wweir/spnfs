/*
 *  linux/fs/nfs/blocklayout/blocklayoutdm.c
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

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

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

/*
 * Release meta device
 */
static int nfs4_blk_metadev_release(struct pnfs_block_dev *bdev)
{
	int rv;

	dprintk("%s Releasing %s\n", __func__, bdev->bm_mdevname);
	/* XXX Check return? */
	rv = nfs4_blkdev_put(bdev->bm_mdev);
	dprintk("%s nfs4_blkdev_put returns %d\n", __func__, rv);

	rv = dev_remove(bdev->bm_mdevname);
	dprintk("%s Returns %d\n", __func__, rv);
	return rv;
}

void free_block_dev(struct pnfs_block_dev *bdev)
{
	if (bdev) {
		if (bdev->bm_mdev) {
			dprintk("%s Removing DM device: %s %d:%d\n",
				__func__,
				bdev->bm_mdevname,
				MAJOR(bdev->bm_mdev->bd_dev),
				MINOR(bdev->bm_mdev->bd_dev));
			/* XXX Check status ?? */
			nfs4_blk_metadev_release(bdev);
		}
		kfree(bdev);
	}
}

/*
 *  Create meta device. Keep it open to use for I/O.
 */
struct pnfs_block_dev *nfs4_blk_init_metadev(struct super_block *sb,
					     struct pnfs_device *dev)
{
	static uint64_t dev_count; /* STUB used for device names */
	struct block_device *bd;
	dev_t meta_dev;
	struct pnfs_block_dev *rv;
	int status;

	dprintk("%s enter\n", __func__);

	rv = kmalloc(sizeof(*rv) + 32, GFP_KERNEL);
	if (!rv)
		return NULL;
	rv->bm_mdevname = (char *)rv + sizeof(*rv);
	sprintf(rv->bm_mdevname, "FRED_%Lu", dev_count++);
	status = dev_create(rv->bm_mdevname, &meta_dev);
	if (status)
		goto out_err;
	bd = nfs4_blkdev_get(meta_dev);
	if (!bd)
		goto out_err;
	if (bd_claim(bd, sb)) {
		dprintk("%s: failed to claim device %d:%d\n",
					__func__,
					MAJOR(meta_dev),
					MINOR(meta_dev));
		blkdev_put(bd);
		goto out_err;
	}

	rv->bm_mdev = bd;
	memcpy(&rv->bm_mdevid, &dev->dev_id, sizeof(struct pnfs_deviceid));
	dprintk("%s Created device %s named %s with bd_block_size %u\n",
				__func__,
				bd->bd_disk->disk_name,
				rv->bm_mdevname,
				bd->bd_block_size);
	return rv;

 out_err:
	kfree(rv);
	return NULL;
}

/* Stub */
int nfs4_blk_flatten(struct pnfs_blk_volume *vols, int size,
		     struct pnfs_block_dev *bdev)
{
	return 0;
}

