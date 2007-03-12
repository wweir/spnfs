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

#define NFS4_PNFS_DEV_MAXCOUNT 16
#define NFS4_PNFS_DEV_MAXSIZE 128

/* Layout driver specific identifier for a mount point.  For each mountpoint
 * a reference is stored in the nfs_server structure.
 */
struct pnfs_mount_type {
	void* mountid;
};

/* Layout driver specific identifier for layout information for a file.
 * Each inode has a specific layout type structure.
 * A reference is stored in the nfs_inode structure.
 */
struct pnfs_layout_type {
	struct pnfs_mount_type* mountid;
	void* layoutid;
	int roc_iomode;	/* iomode to return on close, 0=none */
};

/* Layout driver I/O operations.
 * Either the pagecache or non-pagecache read/write operations must be implemented
 */
struct layoutdriver_io_operations {
	/* Functions that use the pagecache.
	 * If use_pagecache == 1, then these functions must be implemented.
	 */
	ssize_t (*read_pagelist) (struct pnfs_layout_type * layoutid, struct inode *, struct page **pages, unsigned int pgbase, unsigned nr_pages, loff_t offset, size_t count, struct nfs_read_data* nfs_data);
	ssize_t (*write_pagelist) (struct pnfs_layout_type * layoutid, struct inode *, struct page **pages, unsigned int pgbase, unsigned nr_pages, loff_t offset, size_t count, int sync, struct nfs_write_data* nfs_data);

	/* Functions that do not use the pagecache.
	 * If use_pagecache == 0, then these functions must be implemented.
	 */
	ssize_t (*read) (struct pnfs_layout_type * layoutid, struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct pnfs_layout_type * layoutid, struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*readv) (struct pnfs_layout_type * layoutid, struct file *, const struct iovec *, unsigned long, loff_t *);
	ssize_t (*writev) (struct pnfs_layout_type * layoutid, struct file *, const struct iovec *, unsigned long, loff_t *);

	/* Consistency ops */
	int (*fsync) (struct pnfs_layout_type * layoutid, struct file *, struct dentry *, int);
	/* 2 problems:
	 * 1) the page list contains nfs_pages, NOT pages
	 * 2) currently the NFS code doesn't create a page array (as it does with read/write)
	 */
	int (*commit) (struct pnfs_layout_type * layoutid, struct inode *, struct list_head *, int sync, struct nfs_write_data *nfs_data);

	/* Layout information. For each inode, alloc_layout is executed once to retrieve an
	 * inode specific layout structure.  Each subsequent layoutget operation results in
	 * a set_layout call to set the opaque layout in the layout driver.*/
	struct pnfs_layout_type* (*alloc_layout) (struct pnfs_mount_type * mountid, struct inode * inode);
	void (*free_layout) (struct pnfs_layout_type * layoutid, struct inode * inode, loff_t offset, size_t count);
	struct pnfs_layout_type* (*set_layout) (struct pnfs_layout_type * layoutid, struct inode * inode, void* layout);

	int (*setup_layoutcommit) (struct pnfs_layout_type * layoutid, struct inode * inode, struct pnfs_layoutcommit_arg* arg);
	void (*cleanup_layoutcommit) (struct pnfs_layout_type * layoutid, struct inode * inode, struct pnfs_layoutcommit_arg* arg, struct pnfs_layoutcommit_res* res);

	/* Registration information for a new mounted file system
	 */
	struct pnfs_mount_type* (*initialize_mountpoint) (struct super_block *);
	int (*uninitialize_mountpoint) (struct pnfs_mount_type* mountid);

	/* Other ops... */
	int (*ioctl) (struct pnfs_layout_type *, struct inode *, struct file *, unsigned int, unsigned long);
};

struct layoutdriver_policy_operations {
	/* The stripe size of the file system */
	ssize_t (*get_stripesize) (struct pnfs_layout_type * layoutid, struct inode *);

	/* Should the NFS req. gather algorithm cross stripe boundaries? */
	int (*gather_across_stripes) (struct pnfs_mount_type * mountid);

	/* Retreive the block size of the file system.  If gather_across_stripes == 1,
	 * then the file system will gather requests into the block size.
	 * TODO: Where will the layout driver get this info?  It is hard coded in PVFS2.
	 */
	ssize_t (*get_blocksize) (struct pnfs_mount_type *);

	/* Read requests under this value are sent to the NFSv4 server */
	ssize_t (*get_read_threshold) (struct pnfs_layout_type *, struct inode *);

	/* Write requests under this value are sent to the NFSv4 server */
	ssize_t (*get_write_threshold) (struct pnfs_layout_type *, struct inode *);

	/* Use the linux page cache prior to calling layout driver
	 * read/write functions
	 */
	int (*use_pagecache) (struct pnfs_layout_type *, struct inode *);

	/* Should the pNFS client issue a layoutget call in the
	 * same compound as the OPEN operation?
	 */
	int (*layoutget_on_open) (struct pnfs_mount_type *);

	/* Should the pNFS client commit and return the layout upon a setattr
	 */
	int (*layoutret_on_setattr) (struct pnfs_mount_type *);
};

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
	const int id;
	const char *name;
	struct layoutdriver_io_operations *ld_io_ops;
	struct layoutdriver_policy_operations *ld_policy_ops;
};

struct pnfs_device
{
	int           dev_id;
	int           dev_type;
	unsigned int  dev_count;
	unsigned int  dev_addr_len;
	char          dev_addr_buf[NFS4_PNFS_DEV_MAXSIZE];
};

struct pnfs_devicelist {
	unsigned int        num_devs;
	unsigned int        eof;
	unsigned int        devs_len;
	struct pnfs_device  devs[NFS4_PNFS_DEV_MAXCOUNT];
};

/* pNFS client callback functions.
 * These operations allow the layout driver to access pNFS client
 * specific information or call pNFS client->server operations.
 * E.g., getdeviceinfo, I/O callbacks, etc
 */
struct pnfs_client_operations {
	int (*nfs_fsync) (struct file * file, struct dentry * dentry, int datasync);
	int (*nfs_getdevicelist) (struct super_block * sb, struct pnfs_devicelist* devlist);
	int (*nfs_getdeviceinfo) (struct super_block * sb, u32 dev_id, struct pnfs_device * dev);

        /* Post read callback.  Layout driver calls this function if unstable data was
	 * written and requires a commit call
	 */
	void (*nfs_readlist_complete) (struct nfs_read_data* nfs_data, ssize_t status, int eof);

	/* Post write callback.  Layout driver calls this function if unstable data was
	 * written and requires a commit call
	 */
	void (*nfs_writelist_complete) (struct nfs_write_data* nfs_data, ssize_t status);

	/* Post commit callback.  Layout driver calls this function once data is
	 * on stable storage.
	 */
	void (*nfs_commit_complete) (struct nfs_write_data* nfs_data, ssize_t status);
	void (*nfs_return_layout) (struct inode *);
};

extern struct pnfs_client_operations pnfs_ops;

struct pnfs_client_operations* pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *);
void pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *);

#define NFS4_PNFS_MAX_LAYOUTS 4
#define NFS4_PNFS_PRIVATE_LAYOUT 0x80000000

enum pnfs_layouttype4 {
	LAYOUT_NFSV4_FILES  = 1,
	LAYOUT_OSD2_OBJECTS = 2,
	LAYOUT_BLOCK_VOLUME = 3,
	LAYOUT_PVFS2        = 4
};

enum file_layout_device_type {
	FILE_SIMPLE  = 1,
	FILE_COMPLEX = 2
};

#endif /* LINUX_NFS4_PNFS_H */
