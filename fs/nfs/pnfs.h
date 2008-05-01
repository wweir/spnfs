/*
 *  fs/nfs/pnfs.h
 *
 *  pNFS client data structures.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dean Hildebrand   <dhildebz@eecs.umich.edu>
 */

#ifndef FS_NFS_PNFS_H
#define FS_NFS_PNFS_H

#include <linux/nfs4_pnfs.h>
#include <linux/nfs_page.h>
#include <linux/nfs4_pnfs.h>

#ifdef CONFIG_PNFS
/* nfs4proc.c */
extern int nfs4_pnfs_getdevicelist(struct super_block *sb, struct nfs_fh *fh,
				   struct pnfs_devicelist *devlist);
extern int nfs4_pnfs_getdeviceinfo(struct super_block *sb, struct nfs_fh *fh,
				   struct pnfs_device *dev);

/* pnfs.c */
int pnfs_update_layout(struct inode *ino, struct nfs_open_context *ctx,
	size_t count, loff_t pos, enum pnfs_iomode access_type,
	struct pnfs_layout_segment **lsegpp);

int pnfs_return_layout(struct inode *, struct nfs4_pnfs_layout_segment *,
		       enum pnfs_layoutrecall_type);
int pnfs_return_layout_rpc(struct nfs_server *server, struct nfs4_pnfs_layoutreturn_arg *argp);
void set_pnfs_layoutdriver(struct super_block *sb, struct nfs_fh *fh, u32 id);
void unmount_pnfs_layoutdriver(struct super_block *sb);
int pnfs_use_read(struct inode *inode, ssize_t count);
int pnfs_use_ds_io(struct list_head *, struct inode *, int);

int pnfs_use_write(struct inode *inode, ssize_t count);
int _pnfs_try_to_write_data(struct nfs_write_data *, const struct rpc_call_ops *, int);
int _pnfs_try_to_read_data(struct nfs_read_data *data, const struct rpc_call_ops *call_ops);
int pnfs_initialize(void);
void pnfs_uninitialize(void);
void pnfs_layoutcommit_done(struct pnfs_layoutcommit_data *data, int status);
int pnfs_layoutcommit_inode(struct inode *inode, int sync);
void pnfs_update_last_write(struct nfs_inode *nfsi, loff_t offset, size_t extent);
void pnfs_need_layoutcommit(struct nfs_inode *nfsi, struct nfs_open_context *ctx);
int pnfs_enabled_sb(struct nfs_server *nfss);
unsigned int pnfs_getiosize(struct nfs_server *server);
void pnfs_set_ds_iosize(struct nfs_server *server);
int pnfs_commit(struct nfs_write_data *data, int sync);
int _pnfs_try_to_commit(struct nfs_write_data *);
void pnfs_pageio_init_read(struct nfs_pageio_descriptor *, struct inode *, struct nfs_open_context *, struct list_head *, size_t *);
void pnfs_pageio_init_write(struct nfs_pageio_descriptor *, struct inode *);
void pnfs_update_layout_commit(struct inode *, struct list_head *, pgoff_t, unsigned int);
int pnfs_flush_one(struct inode *, struct list_head *, unsigned int, size_t, int);
void pnfs_free_request_data(struct nfs_page *req);
void pnfs_free_fsdata(struct pnfs_fsdata *fsdata);
ssize_t pnfs_file_write(struct file *, const char __user *, size_t, loff_t *);
void pnfs_get_layout_done(struct pnfs_layout_type *,
			  struct nfs4_pnfs_layoutget *, int);
void pnfs_layout_release(struct pnfs_layout_type *);
int _pnfs_write_begin(struct inode *inode, struct page *page,
		      loff_t pos, unsigned len, void **fsdata);
int _pnfs_do_flush(struct inode *inode, struct nfs_page *req,
		   struct pnfs_fsdata *fsdata);

#define PNFS_EXISTS_LDIO_OP(srv, opname) ((srv)->pnfs_curr_ld &&	\
				     (srv)->pnfs_curr_ld->ld_io_ops &&	\
				     (srv)->pnfs_curr_ld->ld_io_ops->opname)
#define PNFS_EXISTS_LDPOLICY_OP(srv, opname) ((srv)->pnfs_curr_ld &&	\
				     (srv)->pnfs_curr_ld->ld_policy_ops && \
				     (srv)->pnfs_curr_ld->ld_policy_ops->opname)

static inline int pnfs_try_to_read_data(struct nfs_read_data *data,
					const struct rpc_call_ops *call_ops)
{
	struct inode *inode = data->inode;
	struct nfs_server *nfss = NFS_SERVER(inode);

	/* FIXME: read_pagelist should probably be mandated */
	if (PNFS_EXISTS_LDIO_OP(nfss, read_pagelist))
		return _pnfs_try_to_read_data(data, call_ops);

	return 1;
}

static inline int pnfs_try_to_write_data(struct nfs_write_data *data,
					 const struct rpc_call_ops *call_ops,
					 int how)
{
	struct inode *inode = data->inode;
	struct nfs_server *nfss = NFS_SERVER(inode);

	/* FIXME: write_pagelist should probably be mandated */
	if (PNFS_EXISTS_LDIO_OP(nfss, write_pagelist))
		return _pnfs_try_to_write_data(data, call_ops, how);

	return 1;
}

static inline int pnfs_try_to_commit(struct nfs_write_data *data)
{
	struct inode *inode = data->inode;
	struct nfs_server *nfss = NFS_SERVER(inode);

	/* Note that we check for "write_pagelist" and not for "commit"
	   since if async writes were done and pages weren't marked as stable
	   the commit method MUST be defined by the LD */
	/* FIXME: write_pagelist should probably be mandated */
	if (PNFS_EXISTS_LDIO_OP(nfss, write_pagelist))
		return _pnfs_try_to_commit(data);

	return 1;
}

static inline int pnfs_write_begin(struct file *filp, struct page *page,
				   loff_t pos, unsigned len, void **fsdata)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct nfs_server *nfss = NFS_SERVER(inode);
	int status = 0;

	*fsdata = NULL;
	if (PNFS_EXISTS_LDIO_OP(nfss, write_begin))
		status = _pnfs_write_begin(inode, page, pos, len, fsdata);
	return status;
}

/* req may not be locked, so we have to be prepared for req->wb_page being
 * set to NULL at any time.
 */
static inline int pnfs_do_flush(struct nfs_page *req, void *fsdata)
{
	struct page *page = req->wb_page;
	struct inode *inode;

	if (!page)
		return 1;
	inode = page->mapping->host;

	if (PNFS_EXISTS_LDPOLICY_OP(NFS_SERVER(inode), do_flush))
		return _pnfs_do_flush(inode, req, fsdata);
	else
		return 0;
}

static inline void pnfs_write_end_cleanup(void *fsdata)
{
	pnfs_free_fsdata(fsdata);
}

static inline void pnfs_redirty_request(struct nfs_page *req)
{
	clear_bit(PG_USE_PNFS, &req->wb_flags);
}

#else  /* CONFIG_PNFS */

static inline int pnfs_try_to_read_data(struct nfs_read_data *data,
					const struct rpc_call_ops *call_ops)
{
	return 1;
}

static inline int pnfs_try_to_write_data(struct nfs_write_data *data,
					 const struct rpc_call_ops *call_ops,
					 int how)
{
	return 1;
}

static inline int pnfs_try_to_commit(struct nfs_write_data *data)
{
	return 1;
}

static inline int pnfs_do_flush(struct nfs_page *req, void *fsdata)
{
	return 0;
}

static inline int pnfs_write_begin(struct file *filp, struct page *page,
				   loff_t pos, unsigned len, void **fsdata)
{
	return 0;
}

static inline void pnfs_write_end_cleanup(void *fsdata)
{
}

static inline void pnfs_redirty_request(struct nfs_page *req)
{
}

#endif /* CONFIG_PNFS */

#endif /* FS_NFS_PNFS_H */
