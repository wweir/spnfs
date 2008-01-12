/*
*  linux/fs/nfsd/nfs4filelayout_xdr.c
*
*  Copyright (c) 2006 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Andy Adamson <andros@umich.edu>
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
*
*
*/
#if defined(CONFIG_PNFSD)

#include <linux/module.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/nfsd/nfs4layoutxdr.h>
#include <linux/nfsd/nfsd4_pnfs.h>

#define NFSDDBG_FACILITY                NFSDDBG_PNFS

/* File layout export_operations->devaddr_encode()  */
int
filelayout_encode_devaddr(u32 *p, u32 *end, int count, void *devl)
{
	struct nfsd4_pnfs_devlist *fdev_list = devl;
	struct pnfs_filelayout_devaddr *fdev;
	int i, len;
	u32 *p_in = p;

	p++;   /* to be set later */

	/* encode device list indices */
	WRITE32(count);

	for (i = 0; i < count; i++)
		WRITE32(i);

	/* encode device list */
	WRITE32(count);

	for (i = 0; i < count; i++) {
		fdev = (struct pnfs_filelayout_devaddr *)fdev_list[i].dev_addr;

		WRITE32(1); /* no multi path for now */
		WRITE32(fdev->r_netid.len);
		WRITEMEM(fdev->r_netid.data, fdev->r_netid.len);
		WRITE32(fdev->r_addr.len);
		WRITEMEM(fdev->r_addr.data, fdev->r_addr.len);
		filelayout_free_devaddr(fdev);
		kfree(fdev);
	}

	/* backfill in length -4 to not include length */
	len = (p - p_in - 1) << 2;
	*p_in = htonl(len);
	len += 4;   /* for blob size */
	printk("%s: count %d len %d\n", __FUNCTION__, count, len);

	return len;
}
EXPORT_SYMBOL(filelayout_encode_devaddr);

int
filelayout_encode_devinfo(u32 *p, u32 *end, int count, void *fdevl)
{
	struct pnfs_filelayout_devaddr *fdev = fdevl;
	int i, len;
	u32 *p_in = p;

	p++;   /* to be set later */

	/* encode device list indices */
	WRITE32(count);

	for (i = 0; i < count; i++)
		WRITE32(i);

	/* encode device list */
	WRITE32(count);

	for (i = 0; i < count; i++) {
		WRITE32(1); /* no multi path for now */
		WRITE32(fdev[i].r_netid.len);
		WRITEMEM(fdev[i].r_netid.data, fdev[i].r_netid.len);
		WRITE32(fdev[i].r_addr.len);
		WRITEMEM(fdev[i].r_addr.data, fdev[i].r_addr.len);
	}

	/* backfill in length */
	len = (p - p_in) << 2;
	*p_in = htonl(len);
	len += 4;   /* for blob size */
	printk("%s: count %d len %d\n", __FUNCTION__, count, len);

	return len;
}
EXPORT_SYMBOL(filelayout_encode_devinfo);

/* File layout export_operations->devaddr_free()  */
void
filelayout_free_devaddr(void *devaddr)
{
	struct pnfs_filelayout_devaddr *fdev;

	fdev = (struct pnfs_filelayout_devaddr *)devaddr;
	if (!fdev)
		return;
	kfree(fdev->r_netid.data);
	kfree(fdev->r_addr.data);
}
EXPORT_SYMBOL(filelayout_free_devaddr);

/* File layout export_operations->layout_encode()  */
__be32
filelayout_encode_layout(u32 *p, u32 *end, void *layout)
{
	struct pnfs_filelayout_layout *flp = (struct pnfs_filelayout_layout *)layout;
	u32 len = 0, *layoutlen_p, nfl_util, fhlen, i;

	dprintk("filelayout_encode_layout: devid %u, fsi %u, numfh %u\n",
		flp->device_id,
		flp->lg_first_stripe_index,
		flp->lg_fh_length);

	/* Ensure room for len, devid, util, and first_stripe_index */
	if (p + XDR_QUADLEN(16) > end)
		return -ENOMEM;

	/* save spot for opaque file layout length, fill-in later*/
	layoutlen_p = p;
	p++;
	len += 4;

	/* encode device id */
	WRITE32(flp->device_id);
	len += 4;

	/* set and encode flags */
	nfl_util = flp->lg_stripe_unit;
	if (flp->lg_commit_through_mds)
		nfl_util |= NFL4_UFLG_COMMIT_THRU_MDS;
	if (flp->lg_stripe_type)
		nfl_util |= NFL4_UFLG_DENSE;
	WRITE32(nfl_util);
	len += 4;

	/* encode first stripe index */
	WRITE32(flp->lg_first_stripe_index);
	len += 4;

	/* Ensure file system added at least one file handle */
	if (flp->lg_fh_length <= 0) {
		printk("%s: File Layout has no file handles!!\n", __FUNCTION__);
		return -NFSERR_LAYOUTUNAVAILABLE;
	}

	/* encode number of file handles */
	if (p + XDR_QUADLEN(4) > end)
		return -ENOMEM;
	WRITE32(flp->lg_fh_length);
	len += 4;

	/* encode file handles */
	for (i = 0; i < flp->lg_fh_length; i++) {
		fhlen = flp->lg_fh_list[i].fh_size;
		if (p + XDR_QUADLEN(4 + fhlen) > end)
			return -ENOMEM;
		WRITE32(fhlen);
		WRITEMEM(&flp->lg_fh_list[i].fh_base, fhlen);
		len += (4 + fhlen);
	}

	/* Set number of bytes encoded =  total_bytes_encoded - length var */
	*layoutlen_p = htonl(len - 4);

	return len;
}
EXPORT_SYMBOL(filelayout_encode_layout);

/* File layout export_operations->layout_free()  */
void
filelayout_free_layout(void *layout)
{
	struct pnfs_filelayout_layout *flp = (struct pnfs_filelayout_layout *)layout;

	if (!flp || !flp->lg_fh_list)
		return;

	kfree(flp->lg_fh_list);
}
EXPORT_SYMBOL(filelayout_free_layout);

#endif /* CONFIG_PNFSD */
