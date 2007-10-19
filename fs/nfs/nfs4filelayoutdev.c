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

void
print_ds_list(struct nfs4_pnfs_dev *fdev)
{
	struct nfs4_pnfs_ds *ds;
	int i;

	ds = fdev->ds_list[0];
	for (i = 0; i < fdev->num_ds; i++) {
		dprintk("        ip_addr %x\n", ntohl(ds->ds_ip_addr));
		dprintk("        port %hu\n", ntohs(ds->ds_port));
		dprintk("        client %p\n", ds->ds_clp);
		dprintk("        cl_exchange_flags %x\n",
				    ds->ds_clp->cl_exchange_flags);
		ds++;
	}
}

void
print_stripe_devs(struct nfs4_pnfs_dev_item *dev)
{
	struct nfs4_pnfs_dev *fdev;
	int i;

	fdev = &dev->stripe_devs[0];
	for (i = 0; i < dev->stripe_count; i++) {
		dprintk("        stripe_index %u\n", fdev->stripe_index);
		dprintk("        num_ds %d\n", fdev->num_ds);
		print_ds_list(fdev);
		fdev++;
	}
}

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
static inline struct nfs4_pnfs_ds *
_data_server_lookup(struct nfs4_pnfs_dev_hlist *hlist, u32 ip_addr, u32 port)
{
	unsigned long      hash;
	struct hlist_node *np;

	dprintk("_data_server_lookup: ip_addr=%x port=%hu\n",
			ntohl(ip_addr), ntohs(port));

	hash = hash_long(ip_addr, NFS4_PNFS_DEV_HASH_BITS);

	hlist_for_each(np, &hlist->dev_dslist[hash]) {
		struct nfs4_pnfs_ds *ds;
		ds = hlist_entry(np, struct nfs4_pnfs_ds, ds_node);
		if (ds->ds_ip_addr == ip_addr &&
		    ds->ds_port == port) {
			return ds;
		}
	}
	return NULL;
}


/* Assumes lock is held */
static inline void
_device_add(struct nfs4_pnfs_dev_hlist *hlist, struct nfs4_pnfs_dev_item *dev)
{
	unsigned long      hash;

	dprintk("_device_add: dev_id=%u stripe_devs:\n", dev->dev_id);
	print_stripe_devs(dev);

	hash = hash_long(dev->dev_id, NFS4_PNFS_DEV_HASH_BITS);
	hlist_add_head(&dev->hash_node, &hlist->dev_list[hash]);
}

/* Assumes lock is held */
static inline void
_data_server_add(struct nfs4_pnfs_dev_hlist *hlist, struct nfs4_pnfs_ds *ds)
{
	unsigned long      hash;

	dprintk("_data_server_add: ip_addr=%x port=%hu\n",
			ntohl(ds->ds_ip_addr), ntohs(ds->ds_port));

	hash = hash_long(ds->ds_ip_addr, NFS4_PNFS_DEV_HASH_BITS);
	hlist_add_head(&ds->ds_node, &hlist->dev_dslist[hash]);
}

/* Create an rpc to the data server defined in 'dev_list' */
static int
device_create(struct rpc_clnt *mds_rpc, struct nfs4_pnfs_dev *dev)
{
	struct nfs4_pnfs_ds *ds;
	struct sockaddr_in	sin;
	int err = 0;

	/* just use the first ds in ds list...*/
	ds = dev->ds_list[0];

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = ds->ds_ip_addr;
	sin.sin_port = ds->ds_port;

	dprintk("device_create: ip=%x, port=%hu, rpcclient %p\n",
			ntohl(ds->ds_ip_addr), ntohs(ds->ds_port), mds_rpc);

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

/* Assumes lock is held */
static int
unhash_ds(struct nfs4_pnfs_ds *ds)
{

	if (!atomic_dec_and_test(&ds->ds_count))
		return 0;

	hlist_del_init(&ds->ds_node);
	return 1;
}

static void
destroy_ds(struct nfs4_pnfs_ds *ds)
{
	nfs4_proc_destroy_session(ds->ds_clp->cl_ds_session,
					  ds->ds_clp->cl_rpcclient);
	rpc_shutdown_client(ds->ds_clp->cl_rpcclient);
	ds->ds_clp->cl_rpcclient = NULL;
	kfree(ds);
}

/* Assumes lock is NOT held */
static void
device_destroy(struct nfs4_pnfs_dev_item *dev,
	       struct nfs4_pnfs_dev_hlist *hlist)
{
	struct nfs4_pnfs_dev *fdev;
	struct nfs4_pnfs_ds *ds;
	HLIST_HEAD(release);
	struct hlist_node *np;
	int i, j;

	if (!dev)
		return;

	dprintk("device_destroy: did=%u dev_list: \n", dev->dev_id);
	print_stripe_devs(dev);

	write_lock(&hlist->dev_lock);
	hlist_del_rcu(&dev->hash_node);

	fdev = &dev->stripe_devs[0];
	for (i = 0; i < dev->stripe_count; i++) {
		for (j = 0; j < fdev->num_ds; j++) {
			ds = fdev->ds_list[j];
			if (ds != NULL && unhash_ds(ds))
				hlist_add_head(&ds->ds_node, &release);
		}
		fdev++;
	}
	write_unlock(&hlist->dev_lock);
	hlist_for_each(np, &release) {
		ds = hlist_entry(np, struct nfs4_pnfs_ds, ds_node);
		destroy_ds(ds);
	}
	kfree(dev->stripe_devs);
	kfree(dev);
}

int
nfs4_pnfs_devlist_init(struct nfs4_pnfs_dev_hlist *hlist)
{
	int i;

	hlist->dev_lock = __RW_LOCK_UNLOCKED("pnfs_devlist_lock");

	for (i = 0; i < NFS4_PNFS_DEV_HASH; i++) {
		INIT_HLIST_HEAD(&hlist->dev_list[i]);
		INIT_HLIST_HEAD(&hlist->dev_dslist[i]);
	}

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
			/* device_destroy grabs hlist->dev_lock */
			device_destroy(dev, hlist);
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
		device_destroy(dev, hlist);
	}

	return 0;
}

static void
nfs4_pnfs_ds_add(struct filelayout_mount_type *mt, struct nfs4_pnfs_ds **dsp,
		 u32 ip_addr, u32 port)
{
	struct nfs4_pnfs_ds *tmp_ds, *ds;
	struct nfs4_pnfs_dev_hlist *hlist = mt->hlist;

	*dsp = NULL;

	ds = kzalloc(sizeof(*tmp_ds), GFP_KERNEL);
	if (!ds)
		return;

	/* Initialize ds */
	ds->ds_ip_addr = ip_addr;
	ds->ds_port = port;
	atomic_set(&ds->ds_count, 1);
	INIT_HLIST_NODE(&ds->ds_node);

	write_lock(&hlist->dev_lock);
	tmp_ds = _data_server_lookup(hlist, ip_addr, port);
	if (tmp_ds == NULL) {
		_data_server_add(hlist, ds);
		*dsp = ds;
	}
	write_unlock(&hlist->dev_lock);
	if (tmp_ds != NULL) {
		dprintk(" data server found, not adding (after creation)\n");
		destroy_ds(ds);
		*dsp = tmp_ds;
	}
}

static struct nfs4_pnfs_ds *
decode_and_add_ds(uint32_t **pp, struct filelayout_mount_type *mt)
{
	struct nfs4_pnfs_ds *ds = NULL;
	char r_addr[29]; /* max size of ip/port string */
	int len;
	u32 ip_addr, port;
	int tmp[6];
	uint32_t *p = *pp;

	dprintk("%s enter\n", __func__);
	/* check and skip r_netid */
	READ32(len);
	/* "tcp" */
	if (len != 3) {
		printk("%s: ERROR: non TCP r_netid len %d\n",
			__func__, len);
		goto out_err;
	}
	/* Read the bytes into a temporary buffer */
	/* TODO: should probably sanity check them */
	READ32(tmp[0]);

	READ32(len);
	if (len > 29) {
		printk("%s: ERROR: Device ip/port too long (%d)\n",
			__func__, len);
		goto out_err;
	}
	COPYMEM(r_addr, len);
	*pp = p;
	r_addr[len] = '\0';
	sscanf(r_addr, "%d.%d.%d.%d.%d.%d", &tmp[0], &tmp[1],
	       &tmp[2], &tmp[3], &tmp[4], &tmp[5]);
	ip_addr = htonl((tmp[0]<<24) | (tmp[1]<<16) | (tmp[2]<<8) | (tmp[3]));
	port = htons((tmp[4] << 8) | (tmp[5]));

	nfs4_pnfs_ds_add(mt, &ds, ip_addr, port);

	/* adding ds to stripe */
	atomic_inc(&ds->ds_count);
	dprintk("%s: addr:port string = %s\n", __func__, r_addr);
	return ds;
out_err:
	dprintk("%s returned NULL\n", __func__);
	return NULL;
}

/* Decode opaque device data and return the result
 */
static struct nfs4_pnfs_dev_item*
decode_device(struct filelayout_mount_type *mt, struct pnfs_device *dev)
{
	int i, len;
	uint32_t *p = (uint32_t *)dev->dev_addr_buf;
	struct nfs4_pnfs_dev_item *file_dev;
	struct nfs4_pnfs_dev *fdev;

	/* Get the stripe count (number of stripe index) */
	READ32(len);
	if (len > NFS4_PNFS_MAX_STRIPE_CNT) {
		printk(KERN_WARNING "%s: stripe count %d greater than "
		       "supported maximum %d\n",
			__func__, len, NFS4_PNFS_MAX_STRIPE_CNT);

		goto out_err;
	}

	file_dev = kzalloc(sizeof(*file_dev), GFP_KERNEL);
	if (!file_dev)
		goto out_err;

	file_dev->stripe_devs = kzalloc(sizeof(struct nfs4_pnfs_dev) * len,
					GFP_KERNEL);
	if (!file_dev->stripe_devs)
		goto out_err_free;
	file_dev->stripe_count = len;

	/* Initialize dev */
	INIT_HLIST_NODE(&file_dev->hash_node);

	/* Device id */
	file_dev->dev_id = dev->dev_id;

	fdev = &file_dev->stripe_devs[0];
	for (i = 0; i < len; i++) {
		READ32(fdev->stripe_index);
		fdev++;
	}

	/* Get the device count, which has to equal the stripe count */
	READ32(len);
	if (len != file_dev->stripe_count) {
		printk("%s: ERROR: device count %d !=  index count %d\n",
			__func__, len, file_dev->stripe_count);
		goto out_err_free;
	}

	fdev = &file_dev->stripe_devs[0];
	for (i = 0; i < file_dev->stripe_count; i++) {
		int j, num;

		/* Get the multipath count for this stripe index */
		READ32(num);
		if (num > NFS4_PNFS_MAX_MULTI_DS) {
			printk(KERN_WARNING
			       "%s: Multipath count %d not supported, "
			       "setting to %d\n",
				__func__, num, NFS4_PNFS_MAX_MULTI_DS);

			num = NFS4_PNFS_MAX_MULTI_DS;
		}

		fdev->num_ds = num;

		for (j = 0; j < fdev->num_ds; j++) {
			fdev->ds_list[j] = decode_and_add_ds(&p, mt);
			if (fdev->ds_list[j] == NULL)
				goto out_err_free;
		}
		fdev++;
	}
	return file_dev;

out_err_free:
	device_destroy(file_dev, mt->hlist);
out_err:
	dprintk("%s ERROR: returning NULL\n", __func__);
	return NULL;
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

	file_dev = decode_device(mt, dev);
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

struct nfs4_pnfs_dev_item *
nfs4_pnfs_device_item_get(struct pnfs_layout_type *ltype, u32 dev_id)
{
	struct filelayout_mount_type *mt;
	struct nfs4_pnfs_dev_item *dev;

	mt = (struct filelayout_mount_type *)ltype->mountid->mountid;

	read_lock(&mt->hlist->dev_lock);
	dev = _device_lookup(mt->hlist, dev_id);
	read_unlock(&mt->hlist->dev_lock);

	if (dev == NULL)
		dev = get_device_info(mt, dev_id);
	return dev;
}

/* Lookup and return the data server struct
 */
struct nfs4_pnfs_dev *
nfs4_pnfs_device_get(struct inode *inode, u32 dev_id, u32 stripe_idx)
{
	struct nfs4_pnfs_dev_item *dev;
	struct nfs_server *server = NFS_SERVER(inode);
	struct filelayout_mount_type *mt;

	mt = (struct filelayout_mount_type *)server->pnfs_mountid->mountid;

	read_lock(&mt->hlist->dev_lock);
	dev = _device_lookup(mt->hlist, dev_id);
	read_unlock(&mt->hlist->dev_lock);
	if (dev == NULL)
		dev = get_device_info(mt, dev_id);

	return &dev->stripe_devs[stripe_idx];
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
	u64 tmp;
	u32 stripe_idx, dbg_stripe_idx;

	if (!layout)
		return 1;

	tmp = offset;
	/* Want ((offset / layout->stripe_unit) % layout->num_devs) */
	do_div(tmp, layout->stripe_unit);
	stripe_idx = do_div(tmp, layout->num_fh) + layout->first_stripe_index;

	/* For debugging */
	tmp = offset + count - 1;
	do_div(tmp, layout->stripe_unit);
	dbg_stripe_idx = do_div(tmp, layout->num_fh) +
				layout->first_stripe_index;

	dprintk("%s: offset=%Lu, count=%u, si=%u, dsi=%u, "
		   "num_devs=%u, stripe_unit=%Lu\n",
		   __func__,
		   offset, count, stripe_idx, dbg_stripe_idx, layout->num_fh,
		   layout->stripe_unit);

	BUG_ON(dbg_stripe_idx != stripe_idx);

	dserver->dev = nfs4_pnfs_device_get(inode, layout->dev_id, stripe_idx);
	if (dserver->dev == NULL)
		return 1;
	dserver->fh = &layout->fh_array[stripe_idx];

	dprintk("%s: dev_id=%u, idx=%u, offset=%Lu, count=%u\n",
		__func__, layout->dev_id, stripe_idx, offset, count);

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
