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
#include <linux/module.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/nfsd/nfs4layoutxdr.h>


/* File layout export_operations->devaddr_encode()  */
int
filelayout_encode_devaddr(u32 *p, u32 *end, void *dev_addr)
{
	struct pnfs_filelayout_devaddr *fdev;
	int len;
	u32 *p_in = p;

	fdev = (struct pnfs_filelayout_devaddr *)dev_addr;
        len = 4+XDR_QUADLEN(fdev->r_netid.len)+XDR_QUADLEN(fdev->r_addr.len);
        len = len << 2;
	if (p + XDR_QUADLEN(len) > end)
		return -ENOMEM;
	WRITE32(len);
	WRITE32(fdev->r_dev_type);
	WRITE32(1);
	WRITE32(fdev->r_netid.len);
	WRITEMEM(fdev->r_netid.data,fdev->r_netid.len);
	WRITE32(fdev->r_addr.len);
	WRITEMEM(fdev->r_addr.data,fdev->r_addr.len);
	return ((p - p_in) << 2);
}
EXPORT_SYMBOL(filelayout_encode_devaddr);

/* File layout export_operations->devaddr_free()  */
void
filelayout_free_devaddr(void *devaddr)
{
	struct pnfs_filelayout_devaddr *fdev;

	fdev = (struct pnfs_filelayout_devaddr *)devaddr;
	if (!fdev)
		return;
	if (fdev->r_netid.data)
		kfree(fdev->r_netid.data);
	if (fdev->r_addr.data)
		kfree(fdev->r_addr.data);
}
EXPORT_SYMBOL(filelayout_free_devaddr);

static int
filelayout_encode_layoutlist_item(u32 *p, u32 *end, struct nfsd4_pnfs_layoutlist *item)
{
	int len;
	unsigned int fhlen = item->fhp->fh_size;

	len = 12 + fhlen;
	if (p + XDR_QUADLEN(len) > end)
		return -ENOMEM;
	WRITE32(item->dev_id);
	WRITE32(item->dev_index);
	WRITE32(fhlen);
	WRITEMEM(&item->fhp->fh_base, fhlen);
	return len;
}

/* File layout export_operations->layout_encode()  */
int
filelayout_encode_layout(u32 *p, u32 *end, void *layout)
{
	struct nfsd4_pnfs_filelayout *flp;
	struct nfsd4_pnfs_layoutlist *item;
	int i, full_len, len;
	u32 *totlen;

	flp = (struct nfsd4_pnfs_filelayout *)layout;
	len = 32;
	if (p + XDR_QUADLEN(len + 4) > end)
		return -ENOMEM;
	full_len = len + 4;
	totlen = p; 	/* fill-in opaque layout length later*/
	p++;
	WRITE32(flp->lg_stripe_type);
	WRITE32(flp->lg_commit_through_mds);
	WRITE64(flp->lg_stripe_unit);
	WRITE64(flp->lg_file_size);
	WRITE32(flp->lg_indexlen);
	if (flp->lg_indexlen > 0) {   //??? if>0 must build index list
		printk("filelayout_encode_layout: XXX add loop for index list\n");
	}
	WRITE32(flp->lg_llistlen);
	for (i=0; i < flp->lg_llistlen; i++) {
		item = &flp->lg_llist[i];
		len = filelayout_encode_layoutlist_item(p, end, item);
		if (len > 0) {
			p += XDR_QUADLEN(len);
			full_len += len;
		}
		else
			break;
	}
	if (len > 0) {
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
	struct nfsd4_pnfs_layoutlist *item;
	int i;

	flp = (struct nfsd4_pnfs_filelayout *)layout;

	if (!flp || !flp->lg_llist)
		return;
	item = flp->lg_llist;
	for (i=0; i < flp->lg_llistlen; i++) {
#if 0 /* the fh is part of nfsd4_pnfs_layoutget struct */
		if (item->fhp)
			kfree(item->fhp);
#endif
		item++;
	}
	kfree(flp->lg_llist);
}
EXPORT_SYMBOL(filelayout_free_layout);

