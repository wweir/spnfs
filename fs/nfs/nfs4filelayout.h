/*
 *  pnfs_nfs4filelayout.h
 *
 *  NFSv4 file layout driver data structures.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dean Hildebrand   <dhildebz@eecs.umich.edu>
 */

#ifndef FS_NFS_NFS4FILELAYOUT_H
#define FS_NFS_NFS4FILELAYOUT_H

#include <linux/kref.h>
#include <linux/nfs4_pnfs.h>
#include <linux/nfs4_session.h>
#include <linux/pnfs_xdr.h>

#define NFS4_PNFS_DEV_HASH_BITS 5
#define NFS4_PNFS_DEV_HASH_SIZE (1 << NFS4_PNFS_DEV_HASH_BITS)
#define NFS4_PNFS_DEV_HASH_MASK (NFS4_PNFS_DEV_HASH_SIZE - 1)

#define NFS4_PNFS_MAX_STRIPE_CNT 16
#define NFS4_PNFS_MAX_MULTI_DS   2

#define FILE_MT(inode) ((struct filelayout_mount_type *) \
			(NFS_SERVER(inode)->pnfs_mountid->mountid))

enum stripetype4 {
	STRIPE_SPARSE = 1,
	STRIPE_DENSE = 2
};

/* Individual ip address */
struct nfs4_pnfs_ds {
	struct hlist_node 	ds_node;  /* nfs4_pnfs_dev_hlist dev_dslist */
	u32 			ds_ip_addr;
	u32 			ds_port;
	struct nfs_client	*ds_clp;
	atomic_t		ds_count;
	char r_addr[29];
};

/* Individual data server with list of mutipath ip's*/
struct nfs4_pnfs_dev {
	u32 			stripe_index;
	int 			num_ds;
	struct nfs4_pnfs_ds	*ds_list[NFS4_PNFS_MAX_MULTI_DS];
};

/* stripe_count is length of dev_list, bounded by NFS4_PNFS_MAX_STRIPE_CNT */
struct nfs4_pnfs_dev_item {
	struct hlist_node	hash_node;   /* nfs4_pnfs_dev_hlist dev_list */
	struct pnfs_deviceid	dev_id;
	u32 			stripe_count;
	struct nfs4_pnfs_dev	*stripe_devs;
};

struct nfs4_pnfs_dev_hlist {
	rwlock_t		dev_lock;
	struct hlist_head	dev_list[NFS4_PNFS_DEV_HASH_SIZE];
	struct hlist_head	dev_dslist[NFS4_PNFS_DEV_HASH_SIZE];
};

/* Actual file layout device (single devid) */
struct nfs4_pnfs_dserver {
	struct nfs_fh        *fh;
	struct nfs4_pnfs_dev *dev;
};

struct nfs4_filelayout_segment {
	u32 stripe_type;
	u32 commit_through_mds;
	u32 stripe_unit;
	u32 first_stripe_index;
	u64 pattern_offset;
	struct pnfs_deviceid dev_id;
	unsigned int num_fh;
	struct nfs_fh fh_array[NFS4_PNFS_MAX_STRIPE_CNT];
};

struct nfs4_filelayout {
	int uncommitted_write;
	loff_t last_commit_size;
	u64 layout_id;
	u32 stripe_unit;
};

struct filelayout_mount_type {
	struct super_block *fl_sb;
	struct nfs4_pnfs_dev_hlist *hlist;
};

extern struct pnfs_client_operations *pnfs_callback_ops;

char *deviceid_fmt(const struct pnfs_deviceid *dev_id);
int  nfs4_pnfs_devlist_init(struct nfs4_pnfs_dev_hlist *hlist);
void nfs4_pnfs_devlist_destroy(struct nfs4_pnfs_dev_hlist *hlist);
int nfs4_pnfs_dserver_get(struct pnfs_layout_segment *lseg,
			  loff_t offset,
			  size_t count,
			  struct nfs4_pnfs_dserver *dserver);
int decode_and_add_devicelist(struct filelayout_mount_type *mt, struct pnfs_devicelist *devlist);
int process_deviceid_list(struct filelayout_mount_type *mt,
			  struct nfs_fh *fh,
			  struct pnfs_devicelist *devlist);
struct nfs4_pnfs_dev_item * nfs4_pnfs_device_item_get(struct filelayout_mount_type *mt,
						      struct nfs_fh *fh,
						      struct pnfs_deviceid *dev_id);
u32 filelayout_dserver_get_index(loff_t offset,
				 struct nfs4_pnfs_dev_item *di,
				 struct nfs4_filelayout_segment *layout);

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

struct nfs4_pnfs_dev_item *
nfs4_pnfs_device_item_get(struct filelayout_mount_type *,
			  struct nfs_fh *,
			  struct pnfs_deviceid *);

#endif /* FS_NFS_NFS4FILELAYOUT_H */
