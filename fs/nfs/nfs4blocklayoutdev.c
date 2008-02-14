/*
 *  linux/fs/nfs/nfs4blocklayoutdev.c
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

#include "nfs4blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_BLOCKLAYOUT

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
struct block_device *nfs4_blkdev_get(dev_t dev)
{
	struct block_device *bd;

	dprintk("%s enter\n", __FUNCTION__);
	bd = open_by_devnum(dev, FMODE_READ);
	if (IS_ERR(bd))
		goto fail;
	return bd;
fail:
	dprintk("%s failed to open device : %ld\n",
			__FUNCTION__, PTR_ERR(bd));
	return NULL;
}

/*
 * Release the block device
 */
int nfs4_blkdev_put(struct block_device *bdev)
{
	dprintk("%s for device %d:%d\n", __FUNCTION__, MAJOR(bdev->bd_dev),
			MINOR(bdev->bd_dev));
	bd_release(bdev);
	return blkdev_put(bdev);
}

/* Add a visible, claimed (by us!) scsi disk to the global list */
static int alloc_add_disk(struct block_device *blk_dev, struct list_head *dlist)
{
	struct visible_block_device *vis_dev;

	dprintk("%s enter\n", __FUNCTION__);
	vis_dev = kmalloc(sizeof(struct visible_block_device), GFP_KERNEL);
	if (!vis_dev) {
		dprintk("%s nfs4_get_sig failed\n", __FUNCTION__);
		return -ENOMEM;
	}
	vis_dev->vi_bdev = blk_dev;
	vis_dev->vi_mapped = 0;
	vis_dev->vi_dev = blk_dev->bd_dev;
	list_add(&vis_dev->vi_node, dlist);
	return 0;
}

/* Walk the list of scsi_devices. Add disks that can be opened and claimed
 * to the temporary global list:  g_dev_list
 */
static int
nfs4_blk_add_scsi_disk(struct super_block *sb, struct Scsi_Host *shost,
		       int index, struct list_head *dlist)
{
	struct block_device *bdev;
	struct gendisk *gd;
	struct scsi_device *sdev;
	unsigned int major, minor, ret = 0;
	dev_t dev;

	dprintk("%s enter \n", __FUNCTION__);
	if (index >= MAX_VOLS) {
		dprintk("%s MAX_VOLS hit\n", __FUNCTION__);
		return -ENOSPC;
	}
	dprintk("%s 1 \n", __FUNCTION__);
	index--;
	shost_for_each_device(sdev, shost) {
		dprintk("%s 2\n", __FUNCTION__);
		/* Need to do this check before bumping index */
		if (sdev->type != TYPE_DISK)
			continue;
		dprintk("%s 3 index %d \n", __FUNCTION__, index);
		if (++index >= MAX_VOLS)
			break;
		major = (!(index >> 4) ? SCSI_DISK0_MAJOR :
			 SCSI_DISK1_MAJOR-1 + (index  >> 4));
		minor =  ((index << 4) & 255);

		dprintk("%s SCSI device %d:%d \n", __FUNCTION__, major, minor);

		dev = MKDEV(major, minor);
		bdev = nfs4_blkdev_get(dev);
		if (!bdev) {
			dprintk("%s: failed to open device %d:%d\n",
					__FUNCTION__, major, minor);
			continue;
		}
		gd = bdev->bd_disk;

		dprintk("%s 4\n", __FUNCTION__);

		if (bd_claim(bdev, sb)) {
			dprintk("%s: failed to claim device %d:%d\n",
				__FUNCTION__, gd->major, gd->first_minor);
			blkdev_put(bdev);
			continue;
		}

		ret = alloc_add_disk(bdev, dlist);
		if (ret < 0)
			goto out_err;
		dprintk("%s ADDED DEVICE capacity %ld, bd_block_size %d\n",
					__FUNCTION__,
					(unsigned long)gd->capacity,
					bdev->bd_block_size);

	}
	index++;
	dprintk("%s returns index %d \n", __FUNCTION__, index);
	return index;

out_err:
	dprintk("%s Can't add disk to list. ERROR: %d\n", __FUNCTION__, ret);
	nfs4_blkdev_put(bdev);
	return ret;
}

/* Destroy the temporary scsi disk list */
void nfs4_blk_destroy_disk_list(struct list_head *dlist)
{
	struct visible_block_device *vis_dev;

	dprintk("%s enter\n", __FUNCTION__);
	while (!list_empty(dlist)) {
		vis_dev = list_first_entry(dlist, struct visible_block_device,
					   vi_node);
		dprintk("%s removing device %d:%d\n", __FUNCTION__,
				MAJOR(vis_dev->vi_dev),
				MINOR(vis_dev->vi_dev));
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
*/
int nfs4_blk_create_scsi_disk_list(struct super_block *sb,
				   struct list_head *dlist)
{
	struct class *class = &shost_class;
	struct class_device *cdev;
	struct Scsi_Host *shost;
	int ret = 0, index = 0;

	dprintk("%s enter\n", __FUNCTION__);

	down(&class->sem);
	list_for_each_entry(cdev, &class->children, node) {
		dprintk("%s 1\n", __FUNCTION__);
		shost = class_to_shost(cdev);
		ret = nfs4_blk_add_scsi_disk(sb, shost, index, dlist);
		dprintk("%s 2 ret %d\n", __FUNCTION__, ret);
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
	int id, i, j;
	uint32_t *p = *pp;
	struct pnfs_blk_volume **array = vols[working].bv_vols;
	for (i = 0; i < vols[working].bv_vol_n; i++) {
		BLK_READBUF(p, end, 4);
		READ32(id);
		/* Need to convert id into index */
		for (j = 0; j < working; j++) {
			if (vols[j].bv_id == id)
				break;
		}
		if (j == working) {
			/* Could not find device id */
			dprintk("Could not find referenced deviceid4 %i "
				"decoding pnfs_block_volume4 with id=%i\n",
				id, vols[j].bv_id);
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
	if (sig->si_num_comps >= MAX_SIG_COMP) {
		dprintk("number of sig components %i >= MAX_SIG_COMP\n",
		       sig->si_num_comps);
		goto out_err;
	}
	for (i = 0; i < sig->si_num_comps; i++) {
		BLK_READBUF(p, end, 20);
		READ64(sig->si_comps[i].bs_offset);
		READ64(sig->si_comps[i].bs_length);
		READ32(tmp); /* This is array length */
		if ((uint32_t) sig->si_comps[i].bs_length != tmp)
			goto out_err;
		BLK_READBUF(p, end, tmp);
		sig->si_comps[i].bs_string = (char *)p;
		p += XDR_QUADLEN(tmp);
	}
	*pp = p;
	return 0;
 out_err:
	return -EIO;
}

/*
 * get_sector()
 * sig_sector is in 512 byte units.
 * If sig_sector is greater or equal to zero, it's from the beginning of
 * the disk. If sig_sector is less than zero, it's from the end of the disk.
 */
static void get_sector(int64_t sig_sector, sector_t *sigblock,
		       struct block_device *bdev, uint64_t *offset_in_sigblock)
{
	int64_t use_sector = sig_sector;
	sector_t local;
	unsigned int size = block_size(bdev);
	unsigned int sizebits = blksize_bits(size);

	dprintk("%s enter. sig_sector %Ld\n", __FUNCTION__, sig_sector);

	if (sig_sector < 0)
		use_sector = bdev->bd_disk->capacity + sig_sector;

	local = (use_sector * 512) >> sizebits;
	*offset_in_sigblock = (use_sector * 512) - (local * size);
	*sigblock = local;

	dprintk("%s sigblock %Lu offset_in_sigblock %Lu\n",
			__FUNCTION__, *sigblock, *offset_in_sigblock);
	return;
}

/*
 * All signatures in sig must be found on bdev for verification.
 * Returns True if sig matches, False otherwise.
 */
static int verify_sig(int64_t sig_sector, struct block_device *bdev,
		      struct pnfs_blk_sig *sig)
{
	sector_t sigblock = 0;
	struct pnfs_blk_sig_comp *comp;
	struct buffer_head *bh = NULL;
	uint64_t offset_in_sigblock = 0;
	char *ptr;
	int num_match, i;

	dprintk("%s enter. bd_disk->capacity %ld, bd_block_size %d\n",
			__FUNCTION__, (unsigned long)bdev->bd_disk->capacity,
			bdev->bd_block_size);

	get_sector(sig_sector, &sigblock, bdev, &offset_in_sigblock);

	dprintk("%s calling bread\n", __FUNCTION__);
	bh = __bread(bdev, sigblock, bdev->bd_block_size);
	if  (!bh)
		goto out_err;

	num_match = sig->si_num_comps;
	for (i = 0; i < sig->si_num_comps; i++) {
		comp = &sig->si_comps[i];
		dprintk("%s comp->bs_offset %Ld\n", __FUNCTION__,
		       comp->bs_offset);
		ptr = (char *)bh->b_data;
		ptr += offset_in_sigblock + comp->bs_offset;
		if (!memcmp(ptr, comp->bs_string, comp->bs_length)) {
			num_match--;
			dprintk("%s Match found: num_match %d\n",
						__FUNCTION__, num_match);
		} else
			goto out_err;
	}
	brelse(bh);
	if (num_match == 0) {
		/* all disk signatures are found, set dev_id */
		dprintk("%s Complete Match Found\n", __FUNCTION__);
		return 1;
	}
out_err:
	dprintk("%s  No Match\n", __FUNCTION__);
	return 0;
}

/*
 * map_sig_to_device()
 * Given a signature, walk the list of visible scsi disks searching for
 * a match. Returns True if mapping was done, False otherwise.
 *
 * While we're at it, fill in the vol->bv_size.
 */
static int map_sig_to_device(int64_t sig_sector, struct pnfs_blk_sig *sig,
			     struct pnfs_blk_volume *vol,
			     struct list_head *sdlist)
{
	int mapped = 0;
	struct visible_block_device *vis_dev;

	list_for_each_entry(vis_dev, sdlist, vi_node) {
		if (vis_dev->vi_mapped)
			continue;
		mapped = verify_sig(sig_sector, vis_dev->vi_bdev, sig);
		if (mapped) {
			vol->bv_dev = vis_dev->vi_bdev->bd_dev;
			vol->bv_size = vis_dev->vi_bdev->bd_disk->capacity;
			vis_dev->vi_mapped = 1;
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
			     struct list_head *sdlist)
{
	int status = 0;
	struct pnfs_blk_sig sig;
	uint32_t *p = *pp;
	int64_t sig_sector;
	struct pnfs_blk_volume *vol = &vols[i];

	BLK_READBUF(p, end, 8);
	READ32(vol->bv_type);
	dprintk("%s vol->bv_type = %i\n", __FUNCTION__, vol->bv_type);
	READ32(vol->bv_id);
	dprintk("%s vol->bv_id = %i\n", __FUNCTION__, vol->bv_id);
	switch (vol->bv_type) {
	case VOLUME_SIMPLE:
		BLK_READBUF(p, end, 8);
		READ64(sig_sector);
		status = decode_blk_signature(&p, end, &sig);
		if (status)
			return status;
		status = map_sig_to_device(sig_sector, &sig, vol, sdlist);
		if (!status) {
			dprintk("Could not find disk for device %i\n",
			       vol->bv_id);
			return -EIO;
		}
		status = 0;
		dprintk("%s Set Simple vol %i to dev %d:%d, size %Li\n",
				__FUNCTION__, vol->bv_id,
				MAJOR(vol->bv_dev),
				MINOR(vol->bv_dev),
				vol->bv_size);
		break;
	case VOLUME_SLICE:
		BLK_READBUF(p, end, 16);
		READ64(vol->bv_offset);
		READ64(vol->bv_size);
		vol->bv_vol_n = 1;
		status = set_vol_array(&p, end, vols, i);
		break;
	case VOLUME_STRIPE:
		BLK_READBUF(p, end, 8);
		READ64(vol->bv_stripe_unit);
		/* Fall through */
	case VOLUME_CONCAT:
		BLK_READBUF(p, end, 4);
		READ32(vol->bv_vol_n);
		if (!vol->bv_vol_n)
			return -EIO;
		status = set_vol_array(&p, end, vols, i);
		vol->bv_size = sum_subvolume_sizes(vol);
		dprintk("%s Set Concat vol %i to size %Li\n",
				__FUNCTION__, vol->bv_id, vol->bv_size);
		break;
	default:
		dprintk("Unknown volume type %i\n", vol->bv_type);
 out_err:
		return -EIO;
	}
	*pp = p;
	return status;
}

/* Decodes pnfs_block_deviceaddr4 (draft-3.5) which is XDR encoded
 * in dev->dev_addr_buf.
 */
static int nfs4_blk_decode_device(struct block_mount_id *b_mt_id,
				  struct pnfs_device *dev,
				  struct list_head *sdlist)
{
	int num_vols, i, rootid, status = -ENOMEM;
	struct pnfs_blk_volume *vols, **arrays;
	uint32_t *p = (uint32_t *)dev->dev_addr_buf;
	uint32_t *end = (uint32_t *) ((char *) p + dev->dev_addr_len);

	dprintk("%s enter\n", __FUNCTION__);

	BLK_READBUF(p, end, 8);
	READ32(rootid);
	dprintk("%s rootid = %i\n", __FUNCTION__, rootid);
	dprintk("%s dev->dev_id = %d\n", __FUNCTION__, dev->dev_id);
	READ32(num_vols);
	dprintk("%s num_vols = %i\n", __FUNCTION__, num_vols);

	vols = kmalloc(sizeof(struct pnfs_blk_volume) * num_vols, GFP_KERNEL);
	if (!vols)
		return status;
	/* Each volume in vols array needs its own array.  Save time by
	 * allocating them all in one large hunk.
	 */
	arrays = kmalloc(sizeof(struct pnfs_blk_volume *) * TOTAL(num_vols),
			 GFP_KERNEL);
	if (!arrays)
		goto out;
	status = 0;

	for (i = 0; i < num_vols; i++) {
		vols[i].bv_vols = SETARRAY(arrays, i, num_vols);
		status = decode_blk_volume(&p, end, vols, i, sdlist);
		if (status)
			goto out;
	}

	/* Check that we have used up opaque */
	if (p != end) {
		dprintk("Undecoded cruft at end of opaque\n");
		status = -EIO;
		goto out;
	}

	/* Now use info in vols to create the meta device */
	status = nfs4_blk_init_mdev(b_mt_id);
	if (status)
		goto out;
	status = nfs4_blk_flatten(vols, num_vols, b_mt_id);
	if (status)
		goto out;

	write_lock(&b_mt_id->bm_lock);
	b_mt_id->bm_mdevid = rootid;
	write_unlock(&b_mt_id->bm_lock);
 out:
	kfree(arrays);
	kfree(vols);
	return status;
 out_err:
	return -EIO;
}

/*
 * NOTE: We do not yet deal with a false eof
 * (the generic pNFS client code does not deal with this...)
 *
 * NOTE assumption that block server will return only a single entry in the list
 */
/* Parse return from GETDEVICELIST and place in b_mt_id.  We search for
 * device sigs among drives in the sdlist.
 */
int
nfs4_blk_process_devicelist(struct block_mount_id *b_mt_id,
			    struct pnfs_devicelist *dl,
			    struct list_head *sdlist)
{
	dprintk("%s enter. dl->num_devs %d dl->layout_type %d dl->eof %d\n",
			__FUNCTION__, dl->num_devs, dl->layout_type, dl->eof);
	if (dl->layout_type != LAYOUT_BLOCK_VOLUME) {
		dprintk("Unexpected layout type %u\n", dl->layout_type);
		return -EIO;
	}
	if (dl->num_devs != 1 || dl->eof != 1) {
		dprintk("STUB - client can't deal with more than one device\n");
		return -EIO;
	}
	return nfs4_blk_decode_device(b_mt_id, &dl->devs[0], sdlist);
}

int
nfs4_blk_process_layoutget(struct pnfs_block_layout *bl,
			   struct nfs4_pnfs_layoutget_res *lgr)
{
	uint32_t *p = (uint32_t *)lgr->layout.buf;
	uint32_t *end = (uint32_t *)((char *)lgr->layout.buf + lgr->layout.len);
	int i, status = -EIO;
	uint32_t count;

	BLK_READBUF(p, end, 8);
	READ32(bl->bl_rootid);
	READ32(count);

	dprintk("%s enter, rootid %i number of extents %i\n", __FUNCTION__,
			bl->bl_rootid, count);
	/* Each extent is 28 bytes */
	BLK_READBUF(p, end, 28 * count);

	for (i = 0; i < count; i++) {
		struct pnfs_block_extent  *be;
		uint64_t tmp; /* Used by READSECTOR */

		be = kmalloc(sizeof(*be), GFP_KERNEL);
		if (!be) {
			status = -ENOMEM;
			goto out_err;
		}
		INIT_LIST_HEAD(&be->be_node);
		kref_init(&be->be_refcnt);
		be->be_bitmap = 0;
		/* The next three values are read in as bytes,
		 * but stored as 512-byte sector lengths
		 */
		READSECTOR(be->be_f_offset);
		READSECTOR(be->be_length);
		READSECTOR(be->be_v_offset);
		READ32(be->be_state);

		spin_lock(&bl->bl_ext_lock);
		list_add_tail(&be->be_node, &bl->bl_extents);
		bl->bl_n_ext++;
		spin_unlock(&bl->bl_ext_lock);
	}
	if (p != end) {
		dprintk("%s Undecoded cruft at end of opaque\n", __FUNCTION__);
		status = -EIO;
		goto out_err;
	}
	status = 0;
 out_err:
	dprintk("%s returns %i\n", __FUNCTION__, status);
	return status;
}
