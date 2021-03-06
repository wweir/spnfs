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

#include <linux/utsname.h>
#include <linux/pnfs_xdr.h>
#include <linux/nfs41_session_recovery.h>
#include "nfs4filelayout.h"
#include "internal.h"
#include "nfs4_fs.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS_LD

struct nfs4_pnfs_dev_item *nfs4_pnfs_device_item_get(
					struct filelayout_mount_type *mt,
					struct nfs_fh *fh,
					struct pnfs_deviceid *dev_id);

void
print_ds_list(struct nfs4_pnfs_dev *fdev)
{
	struct nfs4_pnfs_ds *ds;
	int i;

	ds = fdev->ds_list[0];
	if (ds == NULL) {
		dprintk("%s NULL device \n", __func__);
		return;
	}
	for (i = 0; i < fdev->num_ds; i++) {
		dprintk("        ip_addr %x\n", ntohl(ds->ds_ip_addr));
		dprintk("        port %hu\n", ntohs(ds->ds_port));
		dprintk("        client %p\n", ds->ds_clp);
		dprintk("        ref count %d\n", atomic_read(&ds->ds_count));
		if (ds->ds_clp)
			dprintk("        cl_exchange_flags %x\n",
					    ds->ds_clp->cl_exchange_flags);
		dprintk("        ip:port %s\n", ds->r_addr);
		ds++;
	}
}

void
print_stripe_devs(struct nfs4_pnfs_dev_item *dev)
{
	struct nfs4_pnfs_dev *fdev;
	int i;

	fdev = &dev->stripe_devs[0];
	dprintk("%s dev->stripe_count %d\n", __func__, dev->stripe_count);
	for (i = 0; i < dev->stripe_count; i++) {
		dprintk("        stripe_index %u\n", fdev->stripe_index);
		dprintk("        num_ds %d\n", fdev->num_ds);
		print_ds_list(fdev);
		fdev++;
	}
}

/* Debugging function assuming a 64bit major/minor split of the deviceid */
char *
deviceid_fmt(const struct pnfs_deviceid *dev_id)
{
	static char buf[17];
	uint32_t *p = (uint32_t *)dev_id->data;
	uint64_t major, minor;

	READ64(major);
	READ64(minor);

	sprintf(buf, "%08llu %08llu", major, minor);
	return buf;
}

unsigned long
_deviceid_hash(const struct pnfs_deviceid *dev_id)
{
	unsigned char *cptr = (unsigned char *)dev_id->data;
	unsigned int nbytes = NFS4_PNFS_DEVICEID4_SIZE;
	u64 x = 0;

	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x & NFS4_PNFS_DEV_HASH_MASK;
}

/* Assumes lock is held */
static inline struct nfs4_pnfs_dev_item *
_device_lookup(struct nfs4_pnfs_dev_hlist *hlist,
	       const struct pnfs_deviceid *dev_id)
{
	unsigned long      hash;
	struct hlist_node *np;

	dprintk("_device_lookup: dev_id=%s\n", deviceid_fmt(dev_id));

	hash = _deviceid_hash(dev_id);

	hlist_for_each(np, &hlist->dev_list[hash]) {
		struct nfs4_pnfs_dev_item *dev;
		dev = hlist_entry(np, struct nfs4_pnfs_dev_item, hash_node);
		if (!memcmp(&dev->dev_id, dev_id, NFS4_PNFS_DEVICEID4_SIZE))
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

	dprintk("_device_add: dev_id=%s\nstripe_devs:\n",
		deviceid_fmt(&dev->dev_id));
	print_stripe_devs(dev);

	hash = _deviceid_hash(&dev->dev_id);
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
nfs4_pnfs_ds_create(struct nfs_server *mds_srv, struct nfs4_pnfs_ds *ds)
{
	struct nfs_server	tmp = {
		.nfs_client = NULL,
		.minorversion = 1,
	};
	struct sockaddr_in	sin;
	struct rpc_clnt 	*mds_clnt = mds_srv->client;
	struct nfs_client 	*clp;
	struct rpc_cred		*cred = NULL;
	char			ip_addr[16];
	int			addrlen;
	int err = 0;

	dprintk("--> %s ip:port %s\n", __func__, ds->r_addr);

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = ds->ds_ip_addr;
	sin.sin_port = ds->ds_port;

	/* Set timeout to the mds rpc clnt value.
	 * XXX - find the correct authflavor....
	 *
	 * Fake a client ipaddr (used for sessionid) with hostname
	 * Use hostname since it might be more unique than ipaddr (which
	 * could be set to the loopback 127.0.0/1.1
	 *
	 * XXX: Should sessions continue to use the cl_ipaddr field?
	 */
	addrlen = strnlen(utsname()->nodename, sizeof(utsname()->nodename));
	if (addrlen > sizeof(ip_addr))
		addrlen = sizeof(ip_addr);
	memcpy(ip_addr, utsname()->nodename, addrlen);

	/* XXX need a non-nfs_client struct interface to set up
	 * data server sessions
	 *
	 * tmp: nfs4_set_client sets the nfs_server->nfs_client.
	 *
	 * We specify a retrans and timeout interval equual to MDS. ??
	 */
	err = nfs4_set_client(&tmp,
			      mds_srv->nfs_client->cl_hostname,
			      (struct sockaddr *)&sin,
			      sizeof(struct sockaddr),
			      ip_addr,
			      RPC_AUTH_UNIX,
			      IPPROTO_TCP,
			      mds_clnt->cl_xprt->timeout);
	if (err < 0)
		goto out;

	clp = tmp.nfs_client;

	/* data servers don't renew state */
	cancel_delayed_work(&clp->cl_renewd);

	err = nfs4_init_session(clp, &clp->cl_ds_session, clp->cl_rpcclient);
	if (err)
		goto out_put;

	/* Set exchange id and create session flags
	 *
	 * XXX Need to find the proper credential...
	 */
	dprintk("%s EXCHANGE_ID for clp %p\n", __func__, clp);
	clp->cl_exchange_flags = EXCHGID4_FLAG_USE_PNFS_DS;

	err = _nfs4_proc_exchange_id(clp, cred);
	if (err)
		goto out_put;

	dprintk("%s CREATE_SESSION for clp %p\n", __func__, clp);
	err = nfs41_recover_session_sync(clp->cl_rpcclient, clp,
					 clp->cl_ds_session);
	if (err)
		goto out_put;
	ds->ds_clp = clp;

	dprintk("%s: ip=%x, port=%hu, rpcclient %p\n", __func__,
				ntohl(ds->ds_ip_addr), ntohs(ds->ds_port),
				clp->cl_rpcclient);
out:
	dprintk("%s Returns %d\n", __func__, err);
	return err;
out_put:
	nfs_put_client(clp);
	goto out;
}

static void
destroy_ds(struct nfs4_pnfs_ds *ds)
{
	if (ds->ds_clp) {
		nfs4_proc_destroy_session(ds->ds_clp->cl_ds_session,
					  ds->ds_clp->cl_rpcclient);
		nfs_put_client(ds->ds_clp);
	}
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
	int i, j;

	if (!dev)
		return;

	dprintk("device_destroy: dev_id=%s\ndev_list:\n",
		deviceid_fmt(&dev->dev_id));
	print_stripe_devs(dev);

	write_lock(&hlist->dev_lock);
	hlist_del_init(&dev->hash_node);

	fdev = &dev->stripe_devs[0];
	for (i = 0; i < dev->stripe_count; i++) {
		for (j = 0; j < fdev->num_ds; j++) {
			ds = fdev->ds_list[j];
			if (ds != NULL) {
				/* move to release list */
				hlist_del_init(&ds->ds_node);
				hlist_add_head(&ds->ds_node, &release);
			}
		}
		fdev++;
	}
	write_unlock(&hlist->dev_lock);
	while (!hlist_empty(&release)) {
		ds = hlist_entry(release.first, struct nfs4_pnfs_ds, ds_node);
		hlist_del(&ds->ds_node);
		/* wait until the last reference to destroy the ds session
		 * and nfs_put_client on the ds_clp */
		if (!atomic_dec_and_test(&ds->ds_count))
			ds->ds_clp = NULL;
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

	for (i = 0; i < NFS4_PNFS_DEV_HASH_SIZE; i++) {
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
	for (i = 0; i < NFS4_PNFS_DEV_HASH_SIZE; i++) {
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
	tmp_dev = _device_lookup(hlist, &dev->dev_id);
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
		 u32 ip_addr, u32 port, char *r_addr, int len)
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
	strncpy(ds->r_addr, r_addr, len);
	atomic_set(&ds->ds_count, 1);
	INIT_HLIST_NODE(&ds->ds_node);
	ds->ds_clp = NULL;

	write_lock(&hlist->dev_lock);
	tmp_ds = _data_server_lookup(hlist, ip_addr, port);
	if (tmp_ds == NULL) {
		dprintk("%s add new data server ip 0x%x\n", __func__,
				ds->ds_ip_addr);
		_data_server_add(hlist, ds);
		*dsp = ds;
	}
	write_unlock(&hlist->dev_lock);
	if (tmp_ds != NULL) {
		destroy_ds(ds);
		atomic_inc(&tmp_ds->ds_count);
		dprintk("%s data server found ip 0x%x, inc'ed ds_count to %d\n",
				__func__, tmp_ds->ds_ip_addr,
				atomic_read(&tmp_ds->ds_count));
		*dsp = tmp_ds;
	}
}

static struct nfs4_pnfs_ds *
decode_and_add_ds(uint32_t **pp, struct filelayout_mount_type *mt)
{
	struct nfs_server *mds_srv = NFS_SB(mt->fl_sb);
	struct nfs4_pnfs_ds *ds = NULL;
	char r_addr[29]; /* max size of ip/port string */
	int len, err;
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
	/* XXX: should probably sanity check them */
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

	nfs4_pnfs_ds_add(mt, &ds, ip_addr, port, r_addr, len);

	/* XXX: Do we connect to data servers here?
	 * Don't want a lot of un-used (never used!) connections....
	 * Do we wait until LAYOUTGET which will be called on OPEN?
	 */
	if (!ds->ds_clp) {
		err = nfs4_pnfs_ds_create(mds_srv, ds);
		if (err) {
			printk(KERN_ERR "%s nfs4_pnfs_ds_create error %d\n",
			       __func__, err);
			goto out_err;
		}
	}

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
	memcpy(&file_dev->dev_id, &dev->dev_id, NFS4_PNFS_DEVICEID4_SIZE);

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
		device_destroy(file_dev, mt->hlist);
		return NULL;
	}

	if (nfs4_pnfs_device_add(mt, file_dev))
		return NULL;

	return file_dev;
}

/* For each deviceid, if not already in the cache,
 * call getdeviceinfo and add the devices associated with
 * the deviceid to the list of available devices for this
 * mount point.
 * Must at some point be followed up with device_destroy.
 */
int
process_deviceid_list(struct filelayout_mount_type *mt,
		      struct nfs_fh *fh,
		      struct pnfs_devicelist *devlist)
{
	int i;

	dprintk("--> %s: num_devs=%d\n", __func__, devlist->num_devs);

	for (i = 0; i < devlist->num_devs; i++) {
		if (!nfs4_pnfs_device_item_get(mt, fh, &devlist->dev_id[i])) {
			printk(KERN_WARNING
			       "<-- %s: Error retrieving device %d\n",
			       __func__, i);
			return 1;
		}
	}
	dprintk("<-- %s: success\n", __func__);
	return 0;
}

/* Retrieve the information for dev_id, add it to the list
 * of available devices, and return it.
 */
static struct nfs4_pnfs_dev_item *
get_device_info(struct filelayout_mount_type *mt,
		struct nfs_fh *fh,
		struct pnfs_deviceid *dev_id)
{
	struct pnfs_device *pdev = NULL;
	int rc;

	dprintk("%s mt %p\n", __func__, mt);
	pdev = kmalloc(sizeof(struct pnfs_device), GFP_KERNEL);
	if (pdev == NULL)
		return NULL;

	memcpy(&pdev->dev_id, dev_id, NFS4_PNFS_DEVICEID4_SIZE);
	pdev->layout_type = LAYOUT_NFSV4_FILES;
	/* TODO: Update types when CB_NOTIFY_DEVICEID is available */
	pdev->dev_notify_types = 0;

	rc = pnfs_callback_ops->nfs_getdeviceinfo(mt->fl_sb, fh, pdev);
	dprintk("%s getdevice info returns %d\n", __func__, rc);
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
nfs4_pnfs_device_item_get(struct filelayout_mount_type *mt,
			  struct nfs_fh *fh,
			  struct pnfs_deviceid *dev_id)
{
	struct nfs4_pnfs_dev_item *dev;

	read_lock(&mt->hlist->dev_lock);
	dev = _device_lookup(mt->hlist, dev_id);
	read_unlock(&mt->hlist->dev_lock);

	if (dev == NULL)
		dev = get_device_info(mt, fh, dev_id);
	return dev;
}

/* Want res = ((offset / layout->stripe_unit) % di->stripe_count)
 * Then: ((res + fsi) % di->stripe_count)
 */
u32
filelayout_dserver_get_index(loff_t offset,
			     struct nfs4_pnfs_dev_item *di,
			     struct nfs4_filelayout_segment *layout)
{
	u64 tmp, tmp2;

	tmp = offset;
	do_div(tmp, layout->stripe_unit);
	tmp2 = do_div(tmp, di->stripe_count) + layout->first_stripe_index;
	return do_div(tmp2, di->stripe_count);
}

/* Retrieve the rpc client for a specified byte range
 * in 'inode' by filling in the contents of 'dserver'.
 */
int
nfs4_pnfs_dserver_get(struct pnfs_layout_segment *lseg,
		      loff_t offset,
		      size_t count,
		      struct nfs4_pnfs_dserver *dserver)
{
	struct nfs4_filelayout_segment *layout = LSEG_LD_DATA(lseg);
	struct inode *inode = PNFS_INODE(lseg->layout);
	struct nfs4_pnfs_dev_item *di;
	u64 tmp, tmp2;
	u32 stripe_idx, end_idx;

	if (!layout)
		return 1;

	di = nfs4_pnfs_device_item_get(FILE_MT(inode), NFS_FH(inode),
				       &layout->dev_id);
	if (di == NULL)
		return 1;

	stripe_idx = filelayout_dserver_get_index(offset, di, layout);

	/* For debugging, ensure entire requested range is in this dserver */
	tmp = offset + count - 1;
	do_div(tmp, layout->stripe_unit);
	tmp2 = do_div(tmp, di->stripe_count) + layout->first_stripe_index;
	end_idx = do_div(tmp2, di->stripe_count);

	dprintk("%s: offset=%Lu, count=%Zu, si=%u, dsi=%u, "
		"stripe_count=%u, stripe_unit=%u first_stripe_index %u\n",
		__func__,
		offset, count, stripe_idx, end_idx, di->stripe_count,
		layout->stripe_unit, layout->first_stripe_index);

	BUG_ON(end_idx != stripe_idx);
	BUG_ON(stripe_idx >= di->stripe_count);

	dserver->dev = &di->stripe_devs[stripe_idx];
	if (dserver->dev == NULL) {
		printk(KERN_ERR "%s: No data server for device id (%s)!! \n",
			__func__, deviceid_fmt(&layout->dev_id));
		return 1;
	}
	if (layout->num_fh == 1)
		dserver->fh = &layout->fh_array[0];
	else
		dserver->fh = &layout->fh_array[stripe_idx];

	dprintk("%s: dev_id=%s, ip:port=%s, idx=%u, offset=%Lu, count=%Zu\n",
		__func__, deviceid_fmt(&layout->dev_id),
		dserver->dev->ds_list[0]->r_addr,
		stripe_idx, offset, count);

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
