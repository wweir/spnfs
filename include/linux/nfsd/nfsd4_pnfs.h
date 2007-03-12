/*
*  include/linux/nfsd4_pnfs.h
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

#ifndef _LINUX_NFSD_NFSD4_PNFS_H
#define _LINUX_NFSD_NFSD4_PNFS_H

#include <linux/nfs.h>

/* pNFS structs */

/* This structure gets filled in by the underlying pNFS file system
 * and encoded in nfsd4_encode_devlist_item with help from
 * the gd_ops->layout_encode() callback */
struct nfsd4_pnfs_devlist {
	u32		dev_id;
	void	 	*dev_addr;  /* encoded by callback */
};

struct nfsd4_pnfs_getdevlist {
	u32             		gd_type;        /* request */
	u32				gd_maxcount;    /* request */
	u64				gd_cookie;      /* request - response */
	/* nfs4_verifier */
	u64			        gd_verf;        /* request - response */
	struct export_operations	*gd_ops;
	u32				gd_devlist_len; /* response */
	struct nfsd4_pnfs_devlist 	*gd_devlist;    /*response */
	u32				gd_eof;
};

struct nfsd4_pnfs_getdevinfo {
	u32                             gd_type;      /* request - response */
	u32                             gd_dev_id;    /* request */
	u32                             gd_maxcnt;    /* request */
	struct export_operations	*gd_ops;
	u32				gd_devlist_len; /* response */
	void				*gd_devaddr;    /*response */
};

struct nfsd4_pnfs_layoutget {
	u64				lg_clientid;	/* request */
	u32				lg_signal;	/* request */
	u32				lg_type;	/* request - response */
	u32				lg_iomode;	/* request - response*/
	u64				lg_offset;	/* request - response */
	u64				lg_length;	/* request - response */
	u64				lg_minlength;	/* request */
	u32				lg_mxcnt;	/* request */
	u32				lg_flags;	/* request */
	struct export_operations	*lg_ops;

        /* only for cluster fs file layout 'struct knfsd_fh' */
	unsigned char                   lg_fh[NFS_MAXFHSIZE];

	u32				lg_return_on_close; /* response */
	void				*lg_layout;     /* response callback encoded */
};

struct nfsd4_pnfs_lo_up{
	u32	up_type;
	u32	up_len;     /* layout length */
	void	*up_layout; /* decoded by callback */
};

struct nfsd4_pnfs_layoutcommit {
	u64                     lc_clientid;    /* request */
	u64                     lc_offset;      /* request */
	u64                     lc_length;      /* request */
	u32                     lc_reclaim;     /* request */
	u32                     lc_newoffset;   /* request */
	u64                     lc_last_wr;     /* request */
	u64                     lc_modify_sec;  /* request */
	u32                     lc_modify_nsec; /* request */
	u64                     lc_access_sec;  /* request */
	u32                     lc_access_nsec; /* request */
	struct nfsd4_pnfs_lo_up lc_loup;	/* request */
	u32			lc_size_chg;	/* boolean for response */
	u64                     lc_newsize;     /* response */
};

struct nfsd4_pnfs_layoutreturn {
	u64				lr_clientid;	/* request */
	u32				lr_reclaim;	/* request */
	u32				lr_layout_type;	/* request */
	u32				lr_iomode;	/* request */
	u32				lr_return_type;	/* request */
	u64				lr_offset;	/* request */
	u64				lr_length;	/* request */
	u32				lr_flags;
};

#endif /* _LINUX_NFSD_NFSD4_PNFS_H */
