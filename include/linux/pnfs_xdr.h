/*
 *  include/linux/pnfs_xdr.h
 *
 *  Common xdr data structures needed by pnfs client and server.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 * Dean Hildebrand   <dhildebz@eecs.umich.edu>
 */

#ifndef LINUX_PNFS_XDR_H
#define LINUX_PNFS_XDR_H

#include <linux/nfs4.h>

#define PNFS_LAYOUT_MAXSIZE 4096
#define PNFS_MAX_NUM_LAYOUT_TYPES 2

enum layoutreturn_type {
        LAYOUTRETURN_FILE = 1,
        LAYOUTRETURN_FSID = 2,
        LAYOUTRETURN_ALL = 3,
};

struct nfs4_pnfs_layout {
	__u32 len;
	void *buf;
};

enum pnfs_iomode {
	IOMODE_READ = 1,
	IOMODE_RW = 2,
	IOMODE_ANY = 3,
};

struct nfs4_pnfs_layoutget_arg {
	__u32 type;
	__u32 iomode;
	__u64 offset;
	__u64 length;
	__u64 minlength;
	__u32 maxcount;
	struct nfs_open_context* ctx;
	struct inode* inode;
	void *minorversion_info;
};

struct nfs4_pnfs_layoutget_res {
	__u32 return_on_close;
	__u64 offset;
	__u64 length;
	__u32 iomode;
	__u32 type;
	struct nfs4_pnfs_layout layout;
	void *minorversion_info;
};

struct nfs4_pnfs_layoutget {
	struct nfs4_pnfs_layoutget_arg* args;
	struct nfs4_pnfs_layoutget_res* res;
};

struct pnfs_layoutcommit_arg {
	__u64 lastbytewritten;
	__u32 time_modify_changed;
	struct timespec time_modify;
	__u32 time_access_changed;
	struct timespec time_access;
	const u32 *bitmask;
	struct nfs_fh *fh;

	/* Values set by layout driver */
	__u64 offset;
	__u64 length;
	__u32 layout_type;
	__u32 new_layout_size;
	void* new_layout;
	void *minorversion_info;
};

struct pnfs_layoutcommit_res {
	__u32 sizechanged;
	__u64 newsize;
	struct nfs_fattr *fattr;
	const struct nfs_server *server;
	void *minorversion_info;
};

struct pnfs_layoutcommit_data {
	struct rpc_task task;
	struct inode *inode;
	struct rpc_cred *cred;
	struct nfs_fattr fattr;
        struct nfs_open_context *ctx;
	struct pnfs_layoutcommit_arg args;
        struct pnfs_layoutcommit_res res;
};

struct nfs4_pnfs_layoutreturn_arg {
	__u32	reclaim;
	__u32	layout_type;
	__u32	iomode;
	__u32   return_type;
	__u64	offset;
	__u64	length;
	struct inode* inode;
	void *minorversion_info;
};

struct nfs4_pnfs_layoutreturn_res {
	void *minorversion_info;
};

struct nfs4_pnfs_layoutreturn {
	struct nfs4_pnfs_layoutreturn_arg* args;
	struct nfs4_pnfs_layoutreturn_res *res;
	struct rpc_cred         *cred;
	int rpc_status;
};

struct nfs4_pnfs_getdevicelist_arg {
	const struct nfs_fh *           fh;
	u32                             layoutclass;
	void *minorversion_info;
};

struct nfs4_pnfs_getdevicelist_res {
	struct pnfs_devicelist 		*devlist;
	void 				*minorversion_info;
};

struct nfs4_pnfs_getdeviceinfo_arg {
	const struct nfs_fh *            fh;
	u32                              layoutclass;
	u32                              dev_id;
	void 				*minorversion_info;
};

struct nfs4_pnfs_getdeviceinfo_res {
	struct pnfs_device		*dev;
	void 				*minorversion_info;
};

#endif /* LINUX_PNFS_XDR_H */
