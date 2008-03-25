/*
 * linux/fs/nfs/callback_proc.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback procedures
 */
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "internal.h"

#if defined(CONFIG_PNFS)
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/writeback.h>
#endif

#include "pnfs.h"

#ifdef NFS_DEBUG
#define NFSDBG_FACILITY NFSDBG_CALLBACK
#endif
 
__be32 nfs4_callback_getattr(struct cb_getattrargs *args, struct cb_getattrres *res)
{
	struct nfs_client *clp;
	struct nfs_delegation *delegation;
	struct nfs_inode *nfsi;
	struct inode *inode;

	res->bitmap[0] = res->bitmap[1] = 0;
	res->status = htonl(NFS4ERR_BADHANDLE);
	clp = nfs_find_client(args->addr, 4);
	if (clp == NULL)
		goto out;

	dprintk("NFS: GETATTR callback request from %s\n",
		rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_ADDR));

	inode = nfs_delegation_find_inode(clp, &args->fh);
	if (inode == NULL)
		goto out_putclient;
	nfsi = NFS_I(inode);
	down_read(&nfsi->rwsem);
	delegation = nfsi->delegation;
	if (delegation == NULL || (delegation->type & FMODE_WRITE) == 0)
		goto out_iput;
	res->size = i_size_read(inode);
	res->change_attr = delegation->change_attr;
	if (nfsi->npages != 0)
		res->change_attr++;
	res->ctime = inode->i_ctime;
	res->mtime = inode->i_mtime;
	res->bitmap[0] = (FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE) &
		args->bitmap[0];
	res->bitmap[1] = (FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY) &
		args->bitmap[1];
	res->status = 0;
out_iput:
	up_read(&nfsi->rwsem);
	iput(inode);
out_putclient:
	nfs_put_client(clp);
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, ntohl(res->status));
	return res->status;
}

__be32 nfs4_callback_recall(struct cb_recallargs *args, void *dummy)
{
	struct nfs_client *clp;
	struct inode *inode;
	__be32 res;
	
	res = htonl(NFS4ERR_BADHANDLE);
	clp = nfs_find_client(args->addr, 4);
	if (clp == NULL)
		goto out;

	dprintk("NFS: RECALL callback request from %s\n",
		rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_ADDR));

	do {
		struct nfs_client *prev = clp;

		inode = nfs_delegation_find_inode(clp, &args->fh);
		if (inode != NULL) {
			/* Set up a helper thread to actually return the delegation */
			switch(nfs_async_inode_return_delegation(inode, &args->stateid)) {
				case 0:
					res = 0;
					break;
				case -ENOENT:
					if (res != 0)
						res = htonl(NFS4ERR_BAD_STATEID);
					break;
				default:
					res = htonl(NFS4ERR_RESOURCE);
			}
			iput(inode);
		}
		clp = nfs_find_client_next(prev);
		nfs_put_client(prev);
	} while (clp != NULL);
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, ntohl(res));
	return res;
}

#if defined(CONFIG_PNFS)

/*
 * Retrieve an inode based on layout recall parameters
 *
 * Note: caller must iput(inode) to dereference the inode.
 */
static struct inode *
nfs_layoutrecall_find_inode(struct nfs_client *clp,
			    const struct cb_pnfs_layoutrecallargs *args)
{
	struct nfs_inode *nfsi;
	struct nfs_server *server;
	struct inode *ino = NULL;

	dprintk("%s: Begin recall_type=%d\n", __func__, args->cbl_recall_type);

	down_read(&clp->cl_sem);
	list_for_each_entry(nfsi, &clp->cl_lo_inodes, lo_inodes) {
		if (args->cbl_recall_type == RECALL_FILE) {
		    if (nfs_compare_fh(&args->cbl_fh, &nfsi->fh))
			continue;
		} else if (args->cbl_recall_type == RECALL_FSID) {
			server = NFS_SERVER(&nfsi->vfs_inode);
			if (server->fsid.major != args->cbl_fsid.major ||
			    server->fsid.minor != args->cbl_fsid.minor)
				continue;
		}

		ino = &nfsi->vfs_inode;
		spin_lock(&inode_lock);
		__iget(ino);
		spin_unlock(&inode_lock);
		break;
	}
	up_read(&clp->cl_sem);

	dprintk("%s: Return inode=%p\n", __func__, ino);
	return ino;
}

struct recall_layout_threadargs {
	struct inode *inode;
	struct nfs_client *clp;
	struct completion started;
	struct cb_pnfs_layoutrecallargs rl;
	int result;
};

static int pnfs_recall_layout(void *data)
{
	struct inode *inode, *ino;
	struct nfs_client *clp;
	struct nfs_server *server = NULL;
	struct cb_pnfs_layoutrecallargs rl;
	struct recall_layout_threadargs *args =
		(struct recall_layout_threadargs *)data;
	struct nfs4_pnfs_layoutreturn_arg lr_arg;
	int status;

	daemonize("nfsv4-layoutreturn");

	dprintk("%s: recall_type=%d fsid 0x%llx-0x%llx start\n",
		__func__, args->rl.cbl_recall_type,
		args->rl.cbl_fsid.major, args->rl.cbl_fsid.minor);

	clp = args->clp;
	inode = args->inode;
	server = NFS_SERVER(inode);
	rl = args->rl;
	args->result = 0;
	complete(&args->started);
	args = NULL;
	/* Note: args must not be used after this point!!! */

/* FIXME: need barrier here:
   pause I/O to data servers
   pause layoutgets
   drain all outstanding writes to storage devices
   wait for any outstanding layoutreturns and layoutgets mentioned in
   cb_sequence.
   then return layouts, resume after layoutreturns complete
 */

	if (rl.cbl_recall_type == RECALL_FILE) {
		pnfs_return_layout(inode, &rl.cbl_seg);
		goto out;
	}

	rl.cbl_seg.offset = 0;
	rl.cbl_seg.length = NFS4_LENGTH_EOF;

	/* FIXME: This loop is inefficient, running in O(|s_inodes|^2) */
	while ((ino = nfs_layoutrecall_find_inode(clp, &rl)) != NULL) {
		pnfs_return_layout(ino, &rl.cbl_seg);
		iput(ino);
	}

	/* send final layoutreturn */
	lr_arg.reclaim = 0;
	lr_arg.layout_type = server->pnfs_curr_ld->id;
	lr_arg.return_type = rl.cbl_recall_type;
	lr_arg.lseg = rl.cbl_seg;
	lr_arg.inode = inode;

	status = pnfs_return_layout_rpc(server, &lr_arg);
	if (status)
		printk(KERN_INFO
		       "%s: ignoring pnfs_return_layout_rpc status=%d\n",
		       __func__, status);
out:
	iput(inode);
	module_put_and_exit(0);
	dprintk("%s: exit status %d\n", __func__, 0);
	return 0;
}

/*
 * Asynchronous layout recall!
 */
static int pnfs_async_return_layout(struct nfs_client *clp, struct inode *inode,
				    struct cb_pnfs_layoutrecallargs *rl)
{
	struct recall_layout_threadargs data = {
		.clp = clp,
		.inode = inode,
	};
	struct task_struct *t;
	int status;

	/* should have returned NFS4ERR_NOMATCHING_LAYOUT... */
	BUG_ON(inode == NULL);

	data.rl = *rl;

	init_completion(&data.started);
	__module_get(THIS_MODULE);

	t = kthread_run(pnfs_recall_layout, &data, "%s", "pnfs_recall_layout");
	if (IS_ERR(t)) {
		printk(KERN_INFO "NFS: Layout recall callback thread failed "
			"for client (clientid %08x/%08x)\n",
			(unsigned)(clp->cl_clientid >> 32),
			(unsigned)(clp->cl_clientid));
		status = PTR_ERR(t);
		goto out_module_put;
	}
	wait_for_completion(&data.started);
	return data.result;
out_module_put:
	module_put(THIS_MODULE);
	return status;
}

unsigned pnfs_cb_layoutrecall(struct cb_pnfs_layoutrecallargs *args,
			      void *dummy)
{
	struct nfs_client *clp;
	struct inode *inode = NULL;
	unsigned res;

	res = htonl(NFS4ERR_INVAL);
	clp = nfs_find_client(args->cbl_addr, 4);
	if (clp == NULL) {
		dprintk("%s: no client for addr %u.%u.%u.%u\n",
			__func__, NIPQUAD(args->cbl_addr));
		goto out;
	}

	res = htonl(NFS4ERR_NOMATCHING_LAYOUT);
	inode = nfs_layoutrecall_find_inode(clp, args);
	if (inode == NULL) {
		dprintk("%s: no inode for RECALL_FILE\n", __func__);

		goto out_putclient;
	}

	if (args->cbl_recall_type == RECALL_FILE &&
	    !NFS_I(inode)->current_layout)
		goto out_putinode;

	res = 0;
	/* Set up a helper thread to actually return the delegation */
	if (!pnfs_async_return_layout(clp, inode, args))
		goto out_putclient;

	res = htonl(NFS4ERR_RESOURCE);
out_putinode:
	iput(inode);
out_putclient:
	nfs_put_client(clp);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(res));
	return res;
}

#endif /* defined(CONFIG_PNFS) */

#if defined(CONFIG_NFS_V4_1)

/* FIXME: validate args->cbs_{sequence,slot}id */
/* FIXME: referring calls should be processed */
unsigned nfs4_callback_sequence(struct cb_sequenceargs *args,
				struct cb_sequenceres *res)
{
	int i;
	unsigned status = 0;

	for (i = 0; i < args->csa_nrclists; i++)
		kfree(args->csa_rclists[i].rcl_refcalls);
	kfree(args->csa_rclists);

	memcpy(&res->csr_sessionid, &args->csa_sessionid,
	       sizeof(res->csr_sessionid));
	res->csr_sequenceid = args->csa_sequenceid;
	res->csr_slotid = args->csa_slotid;
	res->csr_highestslotid = NFS41_BC_MAX_CALLBACKS;
	if (res->csr_highestslotid > args->csa_highestslotid)
		res->csr_highestslotid = args->csa_highestslotid;
	res->csr_target_highestslotid = NFS41_BC_MAX_CALLBACKS;

	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	res->csr_status = status;
	return status;
}

#endif /* CONFIG_NFS_V4_1 */
