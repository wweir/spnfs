/*
 *  linux/fs/nfs/nfs4blocklayout.h
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

#include <linux/nfs_fs.h>
#include <linux/pnfs_xdr.h> /* Needed by nfs4_pnfs.h */
#include <linux/nfs4_pnfs.h>
#include <linux/dm-ioctl.h> /* Needed for struct dm_ioctl*/

extern struct class shost_class; /* exported from drivers/scsi/hosts.c */
extern int dm_dev_create(struct dm_ioctl *param); /* from dm-ioctl.c */
extern int dm_dev_remove(struct dm_ioctl *param); /* from dm-ioctl.c */
extern int dm_do_resume(struct dm_ioctl *param);
extern int dm_table_load(struct dm_ioctl *param, size_t param_size);

/*
 * Block layout has one device id used by all layouts for a file system.
 * The one device id maps to an LVM meta device which is configured to the
 * volume topology returned in GETDEVICELIST (which returns a single device id)
 * or GETDEVICEINFO.
 */
/* STUB - this needs to be a list of devices. */
struct block_mount_id {
	struct super_block		*bm_sb;  /* back pointer */
	char				*bm_mdevname; /*meta device name */
	rwlock_t			bm_lock; /* protect fields below */
	uint32_t 			bm_mdevid; /* meta device devid */
	struct block_device		*bm_mdev;   /* meta device */
};

/* holds unverified visible initially non-claimed scsi disk */
struct visible_block_device {
	struct list_head	vi_node;
	struct block_device	*vi_bdev;
	int			vi_mapped;
	dev_t			vi_dev; /* Only used for debug printk */
};


/* OP_GETDEVICELIST and OP_GETDEVICEINFO Decode structures. */

enum blk_vol_type {
	VOLUME_SIMPLE   = 0,	/* maps to a single LU */
	VOLUME_SLICE    = 1,	/* slice of another volume */
	VOLUME_CONCAT   = 2,	/* concatenation of multiple volumes */
	VOLUME_STRIPE   = 3	/* striped across multiple volumes */
};

struct pnfs_blk_volume {
	uint32_t		bv_id;
	uint32_t 		bv_type;
	uint64_t 		bv_size; /* in 512-byte sectors */
	struct pnfs_blk_volume 	**bv_vols;
	int 			bv_vol_n;
	union {
		dev_t			bv_dev;
		uint64_t		bv_stripe_unit;
		uint64_t 		bv_offset;
	};
};

struct pnfs_blk_device {
	uint32_t 		bd_id;
	int 			bd_vol_count;
	struct pnfs_blk_volume	*bd_vols;
	/* other stuff */
};

struct pnfs_blk_sig_comp {
	uint64_t 	bs_offset; /* offset within si_sig_block */
	uint64_t   	bs_length; /* XXX this only needs to be 32 bits */
	char 		*bs_string;
};

/* Maximum number of disk signatures per GETDEVICELIST call */
# define MAX_SIG_COMP 8

/*
 * si_sig_block: location of the 512 byte sector that holds the disk signature
 *     positive => from the beginning of the disk
 *     negative => from the end of the disk
 */
struct pnfs_blk_sig {
	int 				si_num_comps;
	uint64_t			si_sig_block;
	struct pnfs_blk_sig_comp	si_comps[MAX_SIG_COMP];
};

enum exstate4 {
	READ_WRITE_DATA	= 0, /* valid for reading and writing. */
	READ_DATA	= 1, /* valid for reading; it may not be written.*/
	INVALID_DATA	= 2, /* location is valid;  data is invalid */
	NONE_DATA	= 3,  /* location is invalid - it's a hole */
	NEEDS_INIT	= 4  /* INVAL in the process of being upgraded to RW */
};

struct pnfs_block_extent {
	struct list_head be_node;
	sector_t	be_f_offset;  /* the starting offset in the file */
	sector_t	be_length;    /* the size of the extent */
	sector_t	be_v_offset;  /* the starting offset in the volume */
	enum exstate4	be_state;     /* the state of this extent */
	uint32_t	be_bitmap;    /* state tracking for NEEDS_INIT */
	struct kref	be_refcnt;
};

/* XXX Need to rethink this */
struct pnfs_block_layout {
	uint32_t		bl_rootid;      /* logical volume device id */
	spinlock_t		bl_ext_lock;    /* protects bl_extents */
	uint32_t		bl_n_ext;
	struct list_head	bl_extents;
};

#define BLK_ID(lt)	((struct block_mount_id *)(PNFS_MOUNTID(lt)->mountid))
#define BLK_LO(lseg)	((struct pnfs_block_layout *)lseg->ld_data)

uint32_t *blk_overflow(uint32_t *p, uint32_t *end, size_t nbytes);

#define BLK_READBUF(p, e, nbytes)  do { \
	p = blk_overflow(p, e, nbytes); \
	if (!p) { \
		printk(KERN_WARNING \
			"%s: reply buffer overflowed in line %d.\n", \
			__FUNCTION__, __LINE__); \
		goto out_err; \
	} \
} while (0)

/* We can save memory by making this triangular */
#define TOTAL(x) ((x)*(x))
#define SETARRAY(a, i, n) (a + i*n)

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {                  \
	(x) = (uint64_t)ntohl(*p++) << 32;           \
	(x) |= ntohl(*p++);                     \
} while (0)
#define COPYMEM(x,nbytes) do {                  \
	memcpy((x), p, nbytes);                 \
	p += XDR_QUADLEN(nbytes);               \
} while (0)
#define READSECTOR(x)     do { \
	READ64(tmp); \
	if (tmp & 0x1ff) { \
		printk(KERN_WARNING \
		       "%s Value not 512-byte aligned at line %d\n", \
		       __FUNCTION__, __LINE__);			     \
		goto out_err; \
	} \
	(x) = tmp >> 9; \
} while (0)

struct block_device *nfs4_blkdev_get(dev_t dev);
int nfs4_blkdev_put(struct block_device *bdev);
int nfs4_blk_process_devicelist(struct block_mount_id *,
				struct pnfs_devicelist *, struct list_head *);
int nfs4_blk_process_layoutget(struct pnfs_block_layout *bl,
			       struct nfs4_pnfs_layoutget_res *lgr);
int nfs4_blk_create_scsi_disk_list(struct super_block *, struct list_head *);
void nfs4_blk_destroy_disk_list(struct list_head *);
int nfs4_blk_mdev_release(struct block_mount_id *);
int nfs4_blk_init_mdev(struct block_mount_id *);
int nfs4_blk_flatten(struct pnfs_blk_volume *, int, struct block_mount_id *);
