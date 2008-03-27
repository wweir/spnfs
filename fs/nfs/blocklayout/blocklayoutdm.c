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

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

/* Stub */
static int nfs4_blk_metadev_release(struct pnfs_block_dev *bdev)
{
	return 0;
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

/* Stub */
struct pnfs_block_dev *nfs4_blk_init_metadev(struct super_block *sb,
					     struct pnfs_device *dev)
{
	return NULL;
}

/* Stub */
int nfs4_blk_flatten(struct pnfs_blk_volume *vols, int size,
		     struct pnfs_block_dev *bdev)
{
	return 0;
}

