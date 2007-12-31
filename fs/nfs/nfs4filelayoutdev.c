/*
 *  linux/fs/nfs/nfs4filelayoutdev.c
 *
 *  Device operations for the pnfs nfs4 file layout driver.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dean Hildebrand <dhildebz@eecs.umich.edu>
 *  Garth Goodson   <Garth.Goodson@netapp.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef CONFIG_PNFS

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/hash.h>

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include <asm/div64.h>

#include <linux/pnfs_xdr.h>
#include "nfs4filelayout.h"
#include "nfs4_fs.h"

#define NFSDBG_FACILITY		NFSDBG_FILELAYOUT

struct rpc_clnt*
create_nfs_rpcclient(struct rpc_xprt *xprt,
				char *server_name,
				u32 version,
				rpc_authflavor_t authflavor,
				int *err);

/* Assumes lock is held */
static inline struct nfs4_pnfs_dev_item *
_device_lookup(struct nfs4_pnfs_dev_hlist *hlist, u32 dev_id)
{
	unsigned long      hash;
	struct hlist_node *np;

	dprintk("_device_lookup: dev_id=%u\n", dev_id);

	hash = hash_long(dev_id, NFS4_PNFS_DEV_HASH_BITS);

	hlist_for_each(np, &hlist->dev_list[hash]) {
		struct nfs4_pnfs_dev_item *dev;
		dev = hlist_entry(np, struct nfs4_pnfs_dev_item, hash_node);
		if (dev->dev_id == dev_id)
			return dev;
	}
	return NULL;
}

/* Assumes lock is held */
static inline void
_device_add(struct nfs4_pnfs_dev_hlist *hlist, struct nfs4_pnfs_dev_item *dev)
{
	unsigned long      hash;

	dprintk("_device_add: dev_id=%u, ip=%x, port=%hu\n", dev->dev_id,
		ntohl(dev->ip_addr), ntohs(dev->port));

	hash = hash_long(dev->dev_id, NFS4_PNFS_DEV_HASH_BITS);
	hlist_add_head(&dev->hash_node, &hlist->dev_list[hash]);
}

/* Create an rpc to the data server defined in 'dev' */
static int
device_create(struct rpc_clnt *mds_rpc, struct nfs4_pnfs_dev_item *dev)
{
	struct sockaddr_in	sin;
	int err = 0;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = dev->ip_addr;
	sin.sin_port = dev->port;

	dprintk("device_create: dev_id=%u, ip=%x, port=%hu, rpcclient %p\n",
		dev->dev_id, ntohl(dev->ip_addr), ntohs(dev->port), mds_rpc);

	/*
	 * XXX Need to implement.
	 * XXXX Need to port. See fs/nfs/client.c:nfs_create_rpc_client()
	xprt = xprt_create_proto(IPPROTO_TCP, &sin,
					 &mds_rpc->cl_xprt->timeout);
		if (IS_ERR(xprt)) {
			err = PTR_ERR(xprt);
			goto out;
		}

		clp->cl_rpcclient = create_nfs_rpcclient(xprt, "nfs4_pnfs_dserver", mds_rpc->cl_vers, mds_rpc->cl_auth->au_flavor, &err);
		if (clp->cl_rpcclient == NULL) {
			printk(KERN_ERR "%s: Can't create nfs rpc client!\n",
								__FUNCTION__);
			goto out;
		}
	}

	dev->clp = clp;
out:
*/
	return err;
}

static void
device_destroy(struct nfs4_pnfs_dev_item *dev)
{
	if (!dev)
		return;

	dprintk("device_destroy: did=%u, ip=%x, port=%hu, rpcclient %p "
		"flags %x\n",
		dev->dev_id, ntohl(dev->ip_addr), ntohs(dev->port),
		dev->server->client,
		dev->server->nfs_client->cl_exchange_flags);

	/* if not created for DS just return */
	if ((dev->server->nfs_client->cl_exchange_flags &
	     EXCHGID4_FLAG_USE_PNFS_DS) &&
	    !(dev->server->nfs_client->cl_exchange_flags &
	      EXCHGID4_FLAG_USE_PNFS_MDS)) {
		nfs4_proc_destroy_session(dev->server);
		/* BUG_ON(!atomic_sub_and_test(0, &dev->count)); */
		rpc_shutdown_client(dev->server->client);
		dev->server->client = NULL;
	}

	kfree(dev);
}

int
nfs4_pnfs_devlist_init(struct nfs4_pnfs_dev_hlist *hlist)
{
	int i;

	hlist->dev_lock = __RW_LOCK_UNLOCKED("pnfs_devlist_lock");

	for (i = 0; i < NFS4_PNFS_DEV_HASH; i++)
		INIT_HLIST_HEAD(&hlist->dev_list[i]);

	return 0;
}

/* De-alloc all devices for a mount point.  This is called in
 * nfs4_kill_super.
 */
void
nfs4_pnfs_devlist_destroy(struct nfs4_pnfs_dev_hlist *hlist)
{
	int i;

	if (hlist == NULL)
		return;

	/* No lock held, as synchronization should occur at upper levels */
	for (i = 0; i < NFS4_PNFS_DEV_HASH; i++) {
		struct hlist_node *np, *next;

		hlist_for_each_safe(np, next, &hlist->dev_list[i]) {
			struct nfs4_pnfs_dev_item *dev;
			dev = hlist_entry(np, struct nfs4_pnfs_dev_item,
					  hash_node);
			hlist_del_rcu(&dev->hash_node);
			device_destroy(dev);
		}
	}
}

/*
 * Add the device to the list of available devices for this mount point.
 * The * rpc client is created during first I/O.
 */
static int
nfs4_pnfs_device_add(struct filelayout_mount_type *mt,
		     struct nfs4_pnfs_dev_item *dev)
{
	struct nfs4_pnfs_dev_item *tmp_dev;
	struct nfs4_pnfs_dev_hlist *hlist = mt->hlist;

	dprintk("nfs4_pnfs_device_add\n");

	/* Write lock, do lookup again, and then add device */
	write_lock(&hlist->dev_lock);
	tmp_dev = _device_lookup(hlist, dev->dev_id);
	if (tmp_dev == NULL)
		_device_add(hlist, dev);
	write_unlock(&hlist->dev_lock);

	/* Cleanup, if device was recently added */
	if (tmp_dev != NULL) {
		dprintk(" device found, not adding (after creation)\n");
		device_destroy(dev);
	}

	return 0;
}

/* Decode opaque device data and return the result
 */
static struct nfs4_pnfs_dev_item*
decode_device(struct pnfs_device *dev)
{
	int index, i, j, len;
	int tmp[6];
	uint32_t *p = (uint32_t *)dev->dev_addr_buf;
	struct nfs4_pnfs_dev_item *file_dev;
	char r_addr[29]; /* max size of ip/port string */

	file_dev = kmalloc(sizeof(struct nfs4_pnfs_dev_item), GFP_KERNEL);
	if (file_dev == NULL)
		return NULL;
	/* Initialize dev */
	INIT_HLIST_NODE(&file_dev->hash_node);
	atomic_set(&file_dev->count, 0);

	/* Device id */
	file_dev->dev_id = dev->dev_id;

	READ32(index);
	for (i = 0; i < index; i++)  /* skip indices list */
		READ32(j);

	READ32(len);
	BUG_ON(len != 1);    /* 1 DS per device id */

	/* Get the device count */
	READ32(dev->dev_count);

	if (dev->dev_count > 1)
		printk(KERN_NOTICE
			"%s: Add loop for multipath dev_count %d dev_id %d\n",
			__func__, dev->dev_count, dev->dev_id);

	/* Decode contents of device*/

	/* device addr --  r_netid, r_addr */

	/* check and skip r_netid */
	READ32(len);
	/* "tcp" */
	if (len != 3) {
		printk(KERN_ERR
			"%s: ERROR: Device index %d dev_count %d len %d\n",
			__func__, index, dev->dev_count, len);
		kfree(file_dev);
		return NULL;
	}
	/* Read the bytes into a temporary buffer */
	/* TODO: should probably sanity check them */
	READ32(tmp[0]);

	READ32(len);
	if (len > 29) {
		printk(KERN_ERR "%s: ERROR: Device ip/port "
			"string too long (%d)\n", __func__, len);
		kfree(file_dev);
		return NULL;
	}
	memcpy(r_addr, p, len);
	r_addr[len] = '\0';
	sscanf(r_addr, "%d.%d.%d.%d.%d.%d", &tmp[0], &tmp[1],
	       &tmp[2], &tmp[3], &tmp[4], &tmp[5]);
	file_dev->ip_addr = htonl((tmp[0]<<24) | (tmp[1]<<16) |
				  (tmp[2]<<8) | (tmp[3]));
	file_dev->port = htons((tmp[4] << 8) | (tmp[5]));
	dprintk("%s: addr:port string = %s\n", __func__, r_addr);

	return file_dev;
}

/* Decode the opaque device specified in 'dev'
 * and add it to the list of available devices for this
 * mount point.
 * Must at some point be followed up with device_destroy
 */
static struct nfs4_pnfs_dev_item*
decode_and_add_device(struct filelayout_mount_type *mt, struct pnfs_device *dev)
{
	struct nfs4_pnfs_dev_item *file_dev;

	file_dev = decode_device(dev);
	if (!file_dev) {
		printk(KERN_WARNING "%s: Could not decode device\n",
			__func__);
		return NULL;
	}

	if (nfs4_pnfs_device_add(mt, file_dev))
		return NULL;

	return file_dev;
}

/* Decode the opaque device list in 'devlist'
 * and add it to the list of available devices for this
 * mount point.
 * Must at some point be followed up with device_destroy.
 */
int
decode_and_add_devicelist(struct filelayout_mount_type *mt,
			  struct pnfs_devicelist *devlist)
{
	int i, cnt;

	dprintk("%s invoked.  num_devs=%d\n", __func__, devlist->num_devs);

	for (i = 0, cnt = 0;
	     i < devlist->num_devs && cnt < NFS4_PNFS_DEV_MAXCOUNT;
	     i++) {
		if (!decode_and_add_device(mt, &devlist->devs[cnt])) {
			dprintk("%s: error count=%d\n", __func__, cnt);
			return 1;
		}
		cnt++;
	}
	dprintk("%s: success\n", __func__);
	return 0;
}

/* Retrieve the information for dev_id, add it to the list
 * of available devices, and return it.
 */
static struct nfs4_pnfs_dev_item *
get_device_info(struct filelayout_mount_type *mt, u32 dev_id)
{
	int rc;
	struct pnfs_device *pdev = NULL;

	pdev = kmalloc(sizeof(struct pnfs_device), GFP_KERNEL);
	if (pdev == NULL)
		return NULL;

	pdev->dev_id = dev_id;

	rc = pnfs_callback_ops->nfs_getdeviceinfo(mt->fl_sb, dev_id, pdev);
	if (rc) {
		kfree(pdev);
		return NULL;
	}

	/* Found new device, need to decode it and then add it to the
	 * list of known devices for this mountpoint.
	 */
	return decode_and_add_device(mt, pdev);
}

/* Lookup and return the device dev_id
 */
struct nfs4_pnfs_dev_item *
nfs4_pnfs_device_get(struct inode *inode, u32 dev_id)
{
	struct nfs4_pnfs_dev_item *dev;
	struct nfs_server *server = NFS_SERVER(inode);
	struct filelayout_mount_type *mt =
		(struct filelayout_mount_type *)server->pnfs_mountid->mountid;
	struct nfs4_pnfs_dev_hlist *hlist = mt->hlist;

	read_lock(&hlist->dev_lock);
	dev = _device_lookup(hlist, dev_id);
/*
	if (dev) {
		atomic_inc(&dev->count);
	}
*/
	read_unlock(&hlist->dev_lock);
	if (dev == NULL)
		dev = get_device_info(mt, dev_id);

	return dev;
}

/* Retrieve the rpc client for a specified byte range
 * in 'inode' by filling in the contents of 'dserver'.
 */
int
nfs4_pnfs_dserver_get(struct inode *inode,
		      struct nfs4_filelayout *layout,
		      u64 offset,
		      u32 count,
		      struct nfs4_pnfs_dserver *dserver)
{
	u32 dev_id;
	u64 tmp;
	u32 stripe_idx, dbg_stripe_idx;

	if (!layout)
		return 1;

	tmp = offset;
	/* Want ((offset / layout->stripe_unit) % layout->num_devs) */
	do_div(tmp, layout->stripe_unit);
	stripe_idx = do_div(tmp, layout->num_devs);

	/* For debugging */
	tmp = offset + count - 1;
	do_div(tmp, layout->stripe_unit);
	dbg_stripe_idx = do_div(tmp, layout->num_devs);

	dprintk("%s: offset=%Lu, count=%u, si=%u, dsi=%u, "
		   "num_devs=%u, stripe_unit=%Lu\n",
		   __func__,
		   offset, count, stripe_idx, dbg_stripe_idx, layout->num_devs,
		   layout->stripe_unit);

	BUG_ON(dbg_stripe_idx != stripe_idx);

	dev_id = layout->devs[stripe_idx].dev_id;

	dserver->dev_item = nfs4_pnfs_device_get(inode, dev_id);
	if (dserver->dev_item == NULL)
		return 1;
	dserver->fh = &layout->devs[stripe_idx].fh;

	dprintk("%s: dev_id=%u, idx=%u, offset=%Lu, count=%u\n",
		__func__, dev_id, stripe_idx, offset, count);

	return 0;
}

/* Currently not used.
 * I have disabled checking the device count until we can think of a good way
 * to call nfs4_pnfs_device_put in a generic way from the pNFS client.
 * The only way I think think of is to put the nfs4_pnfs_dev_item directly
 * in the nfs4_write/read_data structure, which breaks the clear line between
 * the pNFS client and layout drivers.  If I did do this, then I could call
 * an ioctl on the NFSv4 file layout driver to decrement the device count.
 */
#if 0
static void
nfs4_pnfs_device_put(struct nfs_server *server, struct nfs4_pnfs_dev_hlist *hlist, struct nfs4_pnfs_dev_item *dev)
{
	dprintk("nfs4_pnfs_device_put: dev_id=%u\n", dev->dev_id);
	/* XXX Do we need to invoke this put_client? */
	/* server->rpc_ops->put_client(dev->clp); */
	atomic_dec(&dev->count);
}
#endif

#endif /* CONFIG_PNFS */
