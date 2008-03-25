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

#define NFSDDBG_FACILITY	NFSDDBG_PNFS

/* Encodes the nfsv4_1_file_layout_ds_addr4 structure from draft 13
 * on the response stream.
 * Use linux error codes (not nfs) since these values are being
 * returned to the file system.
 */
int
filelayout_encode_devinfo(struct pnfs_xdr_info *resp, void *device)
{
	unsigned int i, j, len, leadcount;
	u32 *p_in = resp->p;
	struct pnfs_filelayout_device *fdev = device;
	u32 index_count = fdev->fl_stripeindices_length;
	u32 dev_count = fdev->fl_device_length;
	int error = 0;
	int maxcount = resp->maxcount;

	ENCODE_HEAD;

	resp->bytes_written = 0; /* in case there is an error */

	dprintk("%s: Begin indx_cnt: %u dev_cnt: %u\n",
		__func__,
		index_count,
		dev_count);

	/* check space for length, index_count, indexes, dev count */
	leadcount = 4 + 4 + (index_count * 4) + 4;
	RESERVE_SPACE(leadcount);

	maxcount -= leadcount;
	if (maxcount < 0) {
		error =  -ETOOSMALL;
		goto out;
	}
	/* Fill in length later */
	p++;

	/* encode device list indices */
	WRITE32(index_count);

	for (i = 0; i < index_count; i++)
		WRITE32(fdev->fl_stripeindices_list[i]);

	/* encode device list */
	WRITE32(dev_count);
	ADJUST_ARGS();
	for (i = 0; i < dev_count; i++) {
		struct pnfs_filelayout_multipath *mp = &fdev->fl_device_list[i];

		/* encode number of equivalent devices */
		leadcount = 4 + (mp->fl_multipath_length * 20);
		RESERVE_SPACE(leadcount);

		maxcount -= leadcount;
		if (maxcount < 0) {
			error =  -ETOOSMALL;
			goto out;
		}

		WRITE32(mp->fl_multipath_length);
		for (j = 0; j < mp->fl_multipath_length; j++) {
			struct pnfs_filelayout_devaddr *da =
						&mp->fl_multipath_list[j];

			/* Encode device info */
			WRITE32(da->r_netid.len);
			WRITEMEM(da->r_netid.data, da->r_netid.len);
			WRITE32(da->r_addr.len);
			WRITEMEM(da->r_addr.data, da->r_addr.len);
		}
		ADJUST_ARGS();
	}

	/* backfill in length */
	len = (p - p_in) << 2;
	*p_in = htonl(len);
	/* add space for blob size */
	len += 4;

	/* update num bytes written */
	resp->bytes_written = len;

	error = 0;
out:
	dprintk("%s: End err %d xdrlen %d\n",
		__func__, error, resp->bytes_written);
	return error;
}
EXPORT_SYMBOL(filelayout_encode_devinfo);

static int
filelayout_encode_layoutlist_item(u32 *p, u32 *end,
				  struct nfsd4_pnfs_layoutlist *item)
{
	int len;
	unsigned int fhlen = item->dev_fh.fh_size;

	len = 20 + fhlen;
	if (p + XDR_QUADLEN(len) > end)
		return -ENOMEM;
	WRITE32(item->dev_id);
	WRITE32(item->dev_util); /* nfl_util4 */
	WRITE32(0); // FIXME:??? fix in the file system
	WRITE32(1); /* One for now can be an array of FHs */
	WRITE32(fhlen);
	WRITEMEM(&item->dev_fh.fh_base, fhlen);
	return len;
}

/* File layout export_operations->layout_encode()  */
__be32
filelayout_encode_layout(u32 *p, u32 *end, void *layout)
{
	struct nfsd4_pnfs_filelayout *flp;
	struct nfsd4_pnfs_layoutlist *item;
	int full_len, len;
	u32 *totlen;
	u32 nfl_util;

	flp = (struct nfsd4_pnfs_filelayout *)layout;
	len = 4;
	if (p + XDR_QUADLEN(len + 4) > end)
		return -ENOMEM;
	full_len = len + 4;
	totlen = p; 	/* fill-in opaque layout length later*/
	p++;
	nfl_util = flp->lg_stripe_unit;
	if (flp->lg_commit_through_mds)
		nfl_util |= NFL4_UFLG_COMMIT_THRU_MDS;
	if (flp->lg_stripe_type)
		nfl_util |= NFL4_UFLG_DENSE;

	if (flp->lg_indexlen > 0) {   //??? if>0 must build index list
		printk("filelayout_encode_layout: XXX add loop for index list\n");
	}
	item = &flp->lg_llist[0];
	item->dev_util = nfl_util;
	len = filelayout_encode_layoutlist_item(p, end, item);
	if (len > 0) {
		p += XDR_QUADLEN(len);
		full_len += len;

		*totlen = htonl(full_len);
		full_len += 4;
	}
	return full_len;
}
EXPORT_SYMBOL(filelayout_encode_layout);

/* File layout export_operations->layout_free()  */
void
filelayout_free_layout(void *layout)
{
	struct nfsd4_pnfs_filelayout *flp;

	flp = (struct nfsd4_pnfs_filelayout *)layout;

	if (!flp || !flp->lg_llist)
		return;

	kfree(flp->lg_llist);
}
EXPORT_SYMBOL(filelayout_free_layout);

#endif /* CONFIG_PNFSD */
