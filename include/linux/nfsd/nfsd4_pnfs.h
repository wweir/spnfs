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

#if defined(CONFIG_PNFSD)

#include <linux/nfs.h>
#include <linux/nfs_xdr.h>
#include <linux/exportfs.h>
#include <linux/nfsd/nfsfh.h>

/* pNFS structs */

struct nfsd4_pnfs_getdevlist {
	u32             gd_type;	/* request */
	u32		gd_maxnum;	/* request */
	u64		gd_cookie;	/* request - response */
	u64		gd_verf;	/* request - response */
	struct svc_fh 	*gd_fhp;	/* response */
	u32		gd_eof;		/* response */
};

struct nfsd4_pnfs_getdevinfo {
	u32		gd_type;	/* request */
	deviceid_t	gd_devid;	/* request */
	u32		gd_maxcount;	/* request */
	u32		gd_notify_types; /* request */
	struct super_block *gd_sb;
};

struct nfsd4_pnfs_layoutget {
	struct nfsd4_layout_seg	lg_seg;		/* request */
	u32			lg_signal;	/* request */
	u64			lg_minlength;	/* request */
	u32			lg_maxcount;	/* request */
	struct svc_fh		*lg_fhp;	/* response */
	stateid_t		lg_sid;		/* request/response */
};

struct nfsd4_pnfs_layoutcommit {
	struct nfsd4_layout_seg	lc_seg;		/* request */
	u32			lc_reclaim;	/* request */
	u32			lc_newoffset;	/* request */
	u64			lc_last_wr;	/* request */
	struct nfstime4		lc_mtime;	/* request */
	stateid_t		lc_sid;		/* request */
	u32			lc_up_len;	/* layout length */
	void			*lc_up_layout;	/* decoded by callback */
	u32			lc_size_chg;	/* boolean for response */
	u64			lc_newsize;	/* response */
};

enum layoutreturn_flags {
	LR_FLAG_INTERN = 1 << 0,	/* internal return */
	LR_FLAG_EXPIRE = 1 << 1,	/* return on client expiration */
};

struct nfsd4_pnfs_layoutreturn {
	u32			lr_return_type;	/* request */
	struct nfsd4_layout_seg	lr_seg;		/* request */
	u32			lr_reclaim;	/* request */
	u32			lr_flags;
	stateid_t		lr_sid;		/* request/resopnse */
	u32			lrf_body_len;	/* request */
	void			*lrf_body;	/* request */
	u32			lrs_present;	/* response */
};

struct nfsd4_pnfs_open {
	u32			op_create;
	u32			op_createmode;
	int			op_truncate;
	char			op_fn[128];	/* XXX fix constant */
};

struct nfsd4_pnfs_cb_layout {
	u32			cbl_recall_type;	/* request */
	struct nfsd4_layout_seg cbl_seg;		/* request */
	u32			cbl_layoutchanged;	/* request */
	stateid_t		cbl_sid;		/* request */
	struct nfs4_fsid	cbl_fsid;
};

struct nfsd4_pnfs_cb_device {
	u32			cbd_notify_type;	/* request */
	u32			cbd_layout_type;	/* request */
	deviceid_t		cbd_devid;		/* request */
	u32			cbd_immediate;		/* request */
};

/*
 * fh_fsid_type is overloaded to indicate whether a filehandle was one supplied
 * to a DS by LAYOUTGET.  nfs4_preprocess_stateid_op() uses this to decide how
 * to handle a given stateid.
 */
static inline int pnfs_fh_is_ds(struct knfsd_fh *fh)
{
	return fh->fh_fsid_type >= FSID_MAX;
}

static inline void pnfs_fh_mark_ds(struct knfsd_fh *fh)
{
	BUG_ON(fh->fh_version != 1);
	BUG_ON(pnfs_fh_is_ds(fh));
	fh->fh_fsid_type += FSID_MAX;
}

/* allows fh_verify() to check the real fsid_type (i.e., not overloaded). */
static inline int pnfs_fh_fsid_type(struct knfsd_fh *fh)
{
	int fsid_type = fh->fh_fsid_type;

	if (pnfs_fh_is_ds(fh))
		return fsid_type - FSID_MAX;
	return fsid_type;
}
#endif /* CONFIG_PNFSD */

#endif /* _LINUX_NFSD_NFSD4_PNFS_H */
