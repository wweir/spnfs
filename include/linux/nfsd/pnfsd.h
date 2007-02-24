/*
 *  linux/include/nfsd/pnfsd.h
 *
 *  Copyright (c) 2005 The Regents of the University of Michigan.
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

#include <linux/nfsd/nfsd4_pnfs.h>

/* pNFS Metadata to Data server state communication*/
struct pnfs_get_state {
	u32			devid;    /* request */
	stateid_t		stid;     /* request;response */
	clientid_t		clid;     /* response */
	u32			access;    /* response */
	u32			stid_gen;    /* response */
	u32			verifier[2]; /* response */
};

struct pnfs_inval_state {
	struct knfsd_fh 	mdsfh; /* needed only by invalidate all */
	stateid_t		stid;
	clientid_t		clid;
	u32			status;
};

/* pNFS Data Server state */

struct pnfs_ds_stateid {
	struct list_head	ds_hash;        /* ds_stateid hash entry */
	struct list_head	ds_perclid;     /* per client hash entry */
	stateid_t		ds_stid;
	struct knfsd_fh		ds_fh;
	unsigned long		ds_access;
	u32			ds_status;      /* from MDS */
	u32			ds_verifier[2]; /* from MDS */
};

struct pnfs_ds_clientid {
	struct list_head	dc_hash;        /* mds_clid_hashtbl entry */
	struct list_head	dc_stateid;     /* ds_stateid head */
	struct list_head	dc_permdsid;    /* per mdsid hash entry */
	clientid_t		dc_mdsclid;
};

struct pnfs_mds_id {
	struct list_head	di_hash;        /* mds_nodeid list entry */
	struct list_head	di_mdsclid;     /* mds_clientid head */
	uint32_t		di_mdsid;
	time_t			di_mdsboot;	/* mds boot time */
};

void nfsd_layout_recall_cb(struct nfs4_cb_layout *);
int nfs4_pnfs_cb_get_state(struct pnfs_get_state *);
void nfs4_pnfs_state_init(void);
int nfs4_pnfs_get_layout(struct super_block *, struct svc_fh *,
						struct nfsd4_pnfs_layoutget *);
int nfs4_pnfs_return_layout(struct super_block *, struct svc_fh *,
					struct nfsd4_pnfs_layoutreturn *);

