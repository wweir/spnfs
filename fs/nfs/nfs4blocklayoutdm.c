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

/* Stub */
int nfs4_blk_flatten(struct pnfs_blk_volume *vols, int size,
		     struct block_mount_id *b_mt_id)
{
	return 0;
}
