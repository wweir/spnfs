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

#ifdef CONFIG_PNFS
/* nfs4proc.c */
extern int nfs4_pnfs_getdevicelist(struct nfs_fh *fh,
				   struct nfs_server *server,
				   struct pnfs_devicelist *devlist);
extern int nfs4_pnfs_getdeviceinfo(struct nfs_server *server,
				   u32 dev_id,
				   struct pnfs_device *res);

/* pnfs.c */
int virtual_update_layout(struct inode *ino, struct nfs_open_context *ctx,
	size_t count, loff_t pos, enum pnfs_iomode access_type);

int pnfs_return_layout(struct inode *ino, struct nfs4_pnfs_layout_segment *range);
int pnfs_return_layout_rpc(struct nfs_server *server, struct nfs4_pnfs_layoutreturn_arg *argp);
void set_pnfs_layoutdriver(struct super_block *sb, struct nfs_fh *fh, u32 id);
void unmount_pnfs_layoutdriver(struct super_block *sb);
int pnfs_use_read(struct inode *inode, ssize_t count);
int pnfs_use_ds_io(struct list_head *, struct inode *, int);

int pnfs_use_write(struct inode *inode, ssize_t count);
int pnfs_try_to_write_data(struct nfs_write_data *, const struct rpc_call_ops *, int);
int pnfs_try_to_read_data(struct nfs_read_data *data, const struct rpc_call_ops *call_ops);
int pnfs_initialize(void);
void pnfs_uninitialize(void);
void pnfs_layoutcommit_done(struct pnfs_layoutcommit_data *data, int status);
int pnfs_layoutcommit_inode(struct inode *inode, int sync);
void pnfs_writeback_done_update(struct nfs_write_data *);
void pnfs_update_last_write(struct nfs_inode *nfsi, loff_t offset, size_t extent);
void pnfs_need_layoutcommit(struct nfs_inode *nfsi, struct nfs_open_context *ctx);
int pnfs_enabled_sb(struct nfs_server *nfss);
int pnfs_use_nfsv4_wproto(struct inode *inode, ssize_t count);
int pnfs_use_nfsv4_rproto(struct inode *inode, ssize_t count);
unsigned int pnfs_getiosize(struct nfs_server *server);
int pnfs_commit(struct inode *inode, struct list_head *head, int sync, struct nfs_write_data *data);
int pnfs_try_to_commit(struct inode *, struct nfs_write_data *, struct list_head *, int);
int pnfs_rsize(struct inode *, unsigned int, struct nfs_read_data *);
int pnfs_wsize(struct inode *, unsigned int, struct nfs_write_data *);
int pnfs_rpages(struct inode *);
int pnfs_wpages(struct inode *);
void pnfs_readpage_result_norpc(struct rpc_task *task, void *calldata);
void pnfs_writeback_done_norpc(struct rpc_task *, void *);
void pnfs_commit_done_norpc(struct rpc_task *, void *);
#endif /* CONFIG_PNFS */

#endif /* FS_NFS_PNFS_H */
