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

/* Encodes the loc_body structure from draft 13
 * on the response stream.
 * Use linux error codes (not nfs) since these values are being
 * returned to the file system.
 */
int
filelayout_encode_layout(struct pnfs_xdr_info *resp, void *layout)
{
	struct pnfs_filelayout_layout *flp = (struct pnfs_filelayout_layout *)layout;
	u32 len = 0, nfl_util, fhlen, i, leadcount;
	u32 *layoutlen_p = resp->p;
	int maxcount = resp->maxcount;
	int error = 0, maxsize, fhmaxsize;
	ENCODE_HEAD;

	resp->bytes_written = 0; /* in case there is an error */

	dprintk("%s: devid %u, fsi %u, numfh %u\n",
		__func__,
		flp->device_id,
		flp->lg_first_stripe_index,
		flp->lg_fh_length);

	/* Ensure room for len, devid, util, and first_stripe_index */
	leadcount = 20;
	RESERVE_SPACE(leadcount);

	/* Ensure that there is enough space assuming the largest
	 * possible file handle space is utilized
	 */
	fhmaxsize = flp->lg_fh_length * (4 + sizeof(struct knfsd_fh));
	maxsize = leadcount + fhmaxsize;
	maxcount -= maxsize;
	if (maxcount < 0) {
		dprintk("%s: Space_avail: %d Space_req: %d\n",
			__func__, resp->maxcount, maxsize);
		error = -ETOOSMALL;
		goto out;
	}

	/* save spot for opaque file layout length, fill-in later*/
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
		error = -NFSERR_LAYOUTUNAVAILABLE;
		goto out;
	}

	/* encode number of file handles */
	WRITE32(flp->lg_fh_length);
	len += 4;
	ADJUST_ARGS();

	/* encode file handles */
	for (i = 0; i < flp->lg_fh_length; i++) {
		fhlen = flp->lg_fh_list[i].fh_size;
		RESERVE_SPACE(4 + fhlen);
		WRITE32(fhlen);
		WRITEMEM(&flp->lg_fh_list[i].fh_base, fhlen);
		ADJUST_ARGS();
		len += (4 + fhlen);
	}

	/* Set number of bytes encoded =  total_bytes_encoded - length var */
	*layoutlen_p = htonl(len - 4);

	/* update num bytes written */
	resp->bytes_written = len;

	error = 0;
out:
	dprintk("%s: End err %d xdrlen %d\n",
		__func__, error, resp->bytes_written);
	return error;
}
EXPORT_SYMBOL(filelayout_encode_layout);

#endif /* CONFIG_PNFSD */
