/*
 *  include/linux/nfs4_pnfs.h
 *
 *  Common data structures needed by the pnfs client and pnfs layout driver.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dean Hildebrand   <dhildebz@eecs.umich.edu>
 */

#ifndef LINUX_NFS4_PNFS_H
#define LINUX_NFS4_PNFS_H

#if defined(CONFIG_PNFS)

#include <linux/nfs_page.h>

#define NFS4_PNFS_DEV_MAXNUM 16
#define NFS4_PNFS_DEV_MAXSIZE 128

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
	const u32 id;
	const char *name;
	struct layoutdriver_io_operations *ld_io_ops;
	struct layoutdriver_policy_operations *ld_policy_ops;
};

/* Layout driver specific identifier for a mount point.  For each mountpoint
 * a reference is stored in the nfs_server structure.
 */
struct pnfs_mount_type {
	void *mountid;
};

/* Layout driver specific identifier for layout information for a file.
 * Each inode has a specific layout type structure.
 * A reference is stored in the nfs_inode structure.
 */
struct pnfs_layout_type {
	int refcount;
	struct list_head segs;		/* layout segments list */
	int roc_iomode;			/* iomode to return on close, 0=none */
	struct inode *inode;
	nfs4_stateid stateid;
	u8 ld_data[];			/* layout driver private data */
};

static inline struct inode *
PNFS_INODE(struct pnfs_layout_type *lo)
{
	return lo->inode;
}

static inline struct nfs_inode *
PNFS_NFS_INODE(struct pnfs_layout_type *lo)
{
	return NFS_I(PNFS_INODE(lo));
}

static inline struct nfs_server *
PNFS_NFS_SERVER(struct pnfs_layout_type *lo)
{
	return NFS_SERVER(PNFS_INODE(lo));
}

static inline struct pnfs_mount_type *
PNFS_MOUNTID(struct pnfs_layout_type *lo)
{
	return NFS_SERVER(PNFS_INODE(lo))->pnfs_mountid;
}

static inline void *
PNFS_LD_DATA(struct pnfs_layout_type *lo)
{
	return lo->ld_data;
}

static inline struct pnfs_layoutdriver_type *
PNFS_LD(struct pnfs_layout_type *lo)
{
	return NFS_SERVER(PNFS_INODE(lo))->pnfs_curr_ld;
}

static inline struct layoutdriver_io_operations *
PNFS_LD_IO_OPS(struct pnfs_layout_type *lo)
{
	return PNFS_LD(lo)->ld_io_ops;
}

static inline struct layoutdriver_policy_operations *
PNFS_LD_POLICY_OPS(struct pnfs_layout_type *lo)
{
	return PNFS_LD(lo)->ld_policy_ops;
}

struct pnfs_layout_segment {
	struct list_head fi_list;
	struct nfs4_pnfs_layout_segment range;
	struct kref kref;
	struct pnfs_layout_type *layout;
	u8 ld_data[];			/* layout driver private data */
};

static inline void *
LSEG_LD_DATA(struct pnfs_layout_segment *lseg)
{
	return lseg->ld_data;
}

/* Layout driver I/O operations.
 * Either the pagecache or non-pagecache read/write operations must be implemented
 */
struct layoutdriver_io_operations {
	/* Functions that use the pagecache.
	 * If use_pagecache == 1, then these functions must be implemented.
	 */
	/* read and write pagelist should return just 0 (success) or a
	 * negative error code.
	 */
	int (*read_pagelist) (struct pnfs_layout_type *layoutid,
			      struct page **pages, unsigned int pgbase,
			      unsigned nr_pages, loff_t offset, size_t count,
			      struct nfs_read_data *nfs_data);
	int (*write_pagelist) (struct pnfs_layout_type *layoutid,
			       struct page **pages, unsigned int pgbase,
			       unsigned nr_pages, loff_t offset, size_t count,
			       int sync, struct nfs_write_data *nfs_data);
	int (*flush_one) (struct pnfs_layout_segment *, struct list_head *head, unsigned int npages, size_t count, int how);
	void (*free_request_data) (struct nfs_page *);


	/* Consistency ops */
	/* 2 problems:
	 * 1) the page list contains nfs_pages, NOT pages
	 * 2) currently the NFS code doesn't create a page array (as it does with read/write)
	 */
	int (*commit) (struct pnfs_layout_type *layoutid, int sync, struct nfs_write_data *nfs_data);

	/* Layout information. For each inode, alloc_layout is executed once to retrieve an
	 * inode specific layout structure.  Each subsequent layoutget operation results in
	 * a set_layout call to set the opaque layout in the layout driver.*/
	struct pnfs_layout_type * (*alloc_layout) (struct pnfs_mount_type *mountid, struct inode *inode);
	void (*free_layout) (struct pnfs_layout_type *layoutid);
	struct pnfs_layout_segment * (*alloc_lseg) (struct pnfs_layout_type *layoutid, struct nfs4_pnfs_layoutget_res *lgr);
	void (*free_lseg) (struct pnfs_layout_segment *lseg);

	int (*setup_layoutcommit) (struct pnfs_layout_type *layoutid, struct pnfs_layoutcommit_arg *arg);
	void (*cleanup_layoutcommit) (struct pnfs_layout_type *layoutid, struct pnfs_layoutcommit_arg *arg, struct pnfs_layoutcommit_res *res);

	/* Registration information for a new mounted file system
	 */
	struct pnfs_mount_type * (*initialize_mountpoint) (struct super_block *, struct nfs_fh *fh);
	int (*uninitialize_mountpoint) (struct pnfs_mount_type *mountid);

	/* Other ops... */
	int (*ioctl) (struct pnfs_layout_type *, struct inode *, struct file *, unsigned int, unsigned long);
};

struct layoutdriver_policy_operations {
	/* The stripe size of the file system */
	ssize_t (*get_stripesize) (struct pnfs_layout_type *layoutid);

	/* Should the NFS req. gather algorithm cross stripe boundaries? */
	int (*gather_across_stripes) (struct pnfs_mount_type *mountid);

	/* test for nfs page cache coalescing */
	int (*pg_test)(struct nfs_pageio_descriptor *, struct nfs_page *, struct nfs_page *);

	/* Retreive the block size of the file system.  If gather_across_stripes == 1,
	 * then the file system will gather requests into the block size.
	 * TODO: Where will the layout driver get this info?  It is hard coded in PVFS2.
	 */
	ssize_t (*get_blocksize) (struct pnfs_mount_type *);

	/* Read requests under this value are sent to the NFSv4 server */
	ssize_t (*get_read_threshold) (struct pnfs_layout_type *, struct inode *);

	/* Write requests under this value are sent to the NFSv4 server */
	ssize_t (*get_write_threshold) (struct pnfs_layout_type *, struct inode *);

	/* Should the pNFS client issue a layoutget call in the
	 * same compound as the OPEN operation?
	 */
	int (*layoutget_on_open) (struct pnfs_mount_type *);

	/* Should the pNFS client commit and return the layout upon a setattr
	 */
	int (*layoutret_on_setattr) (struct pnfs_mount_type *);

	/* Should the full nfs rpc cleanup code be used after io */
	int (*use_rpc_code) (void);
};

struct pnfs_device {
	struct pnfs_deviceid dev_id;
	unsigned int  layout_type;
	unsigned int  dev_count;
	unsigned int  dev_addr_len;
	char          dev_addr_buf[NFS4_PNFS_DEV_MAXSIZE];
	unsigned int  dev_notify_types;
};

struct pnfs_devicelist {
	unsigned int		eof;
	unsigned int		num_devs;
	struct pnfs_deviceid	dev_id[NFS4_PNFS_DEV_MAXNUM];
};

/* pNFS client callback functions.
 * These operations allow the layout driver to access pNFS client
 * specific information or call pNFS client->server operations.
 * E.g., getdeviceinfo, I/O callbacks, etc
 */
struct pnfs_client_operations {
	int (*nfs_getdevicelist) (struct super_block *sb, struct nfs_fh *fh,
				  struct pnfs_devicelist *devlist);
	int (*nfs_getdeviceinfo) (struct super_block *sb, struct nfs_fh *fh,
				  struct pnfs_device *dev);

	/* Post read callback. */
	void (*nfs_readlist_complete) (struct nfs_read_data *nfs_data);

	/* Post write callback. */
	void (*nfs_writelist_complete) (struct nfs_write_data *nfs_data);

	/* Post commit callback. */
	void (*nfs_commit_complete) (struct nfs_write_data *nfs_data);
	void (*nfs_return_layout) (struct inode *);
};

extern struct pnfs_client_operations pnfs_ops;

extern struct pnfs_client_operations *pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *);
extern void pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *);

#define NFS4_PNFS_MAX_LAYOUTS 4
#define NFS4_PNFS_PRIVATE_LAYOUT 0x80000000

#endif /* CONFIG_PNFS */

#endif /* LINUX_NFS4_PNFS_H */
