/*
*  linux/fs/nfsd/nfs4layoutxdr.h
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
*/

#ifndef NFSD_NFS4LAYOUTXDR_H
#define NFSD_NFS4LAYOUTXDR_H

#if defined(CONFIG_PNFSD)

/* Macros from fs/nfsd/nfs4xdr.c */
#define ENCODE_HEAD              __be32 *p
#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {                           \
	*p++ = htonl((u32)((n) >> 32));                         \
	*p++ = htonl((u32)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	*(p + XDR_QUADLEN(nbytes) - 1) = 0;			\
	memcpy(p, ptr, nbytes);					\
	p += XDR_QUADLEN(nbytes);				\
} while (0)
#define RESERVE_SPACE(nbytes)	do {				\
	p = resp->p;						\
	BUG_ON(p + XDR_QUADLEN(nbytes) > resp->end);		\
} while (0)
#define ADJUST_ARGS()           resp->p = p

/* the nfsd4_pnfs_devlist dev_addr for the file layout type */
struct pnfs_filelayout_devaddr {
	struct xdr_netobj	r_netid;
	struct xdr_netobj	r_addr;
};

/* list of multipath servers */
struct pnfs_filelayout_multipath {
	u32				fl_multipath_length;
	struct pnfs_filelayout_devaddr 	*fl_multipath_list;
};

struct pnfs_filelayout_device {
	u32					fl_stripeindices_length;
	u32       		 		*fl_stripeindices_list;
	u32					fl_device_length;
	struct pnfs_filelayout_multipath 	*fl_device_list;
};

struct pnfs_filelayout_layout {
	u32                             lg_layout_type; /* response */
	u32                             lg_stripe_type; /* response */
	u32                             lg_commit_through_mds; /* response */
	u64                             lg_stripe_unit; /* response */
	u64                             lg_pattern_offset; /* response */
	u32                             lg_first_stripe_index;	/* response */
	deviceid_t			device_id;		/* response */
	u32                             lg_fh_length;		/* response */
	struct knfsd_fh                 *lg_fh_list;		/* response */
};

enum stripetype4 {
	STRIPE_SPARSE = 1,
	STRIPE_DENSE = 2
};

#endif /* CONFIG_PNFSD */

#endif /* NFSD_NFS4LAYOUTXDR_H */
