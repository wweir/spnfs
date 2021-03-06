/*
 *  include/linux/nfsd/xdr4.h
 *
 *  Server-side types for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
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

#ifndef _LINUX_NFSD_XDR4_H
#define _LINUX_NFSD_XDR4_H

#include <linux/nfs4.h>
#include <linux/nfsd/nfsd4_pnfs.h>

#define NFSD4_MAX_TAGLEN	128
#define XDR_LEN(n)                     (((n) + 3) & ~3)

struct nfsd4_compound_state {
	struct svc_fh current_fh;
	struct svc_fh save_fh;
	struct nfs4_stateowner *replay_owner;
#if defined(CONFIG_NFSD_V4_1)
	struct current_session *current_ses;
#endif /* CONFIG_NFSD_V4_1 */
};

static inline u32 nfsd4_compound_minorversion(struct nfsd4_compound_state *cs)
{
#if defined(CONFIG_NFSD_V4_1)
	return cs->current_ses ? 1 : 0;
#else  /* CONFIG_NFSD_V4_1 */
	return 0;
#endif /* CONFIG_NFSD_V4_1 */
}

struct nfsd4_change_info {
	u32		atomic;
	u32		before_ctime_sec;
	u32		before_ctime_nsec;
	u32		after_ctime_sec;
	u32		after_ctime_nsec;
};

struct nfsd4_access {
	u32		ac_req_access;      /* request */
	u32		ac_supported;       /* response */
	u32		ac_resp_access;     /* response */
};

struct nfsd4_close {
	u32		cl_seqid;           /* request */
	stateid_t	cl_stateid;         /* request+response */
	struct nfs4_stateowner * cl_stateowner;	/* response */
};

struct nfsd4_commit {
	u64		co_offset;          /* request */
	u32		co_count;           /* request */
	nfs4_verifier	co_verf;            /* response */
};

struct nfsd4_create {
	u32		cr_namelen;         /* request */
	char *		cr_name;            /* request */
	u32		cr_type;            /* request */
	union {                             /* request */
		struct {
			u32 namelen;
			char *name;
		} link;   /* NF4LNK */
		struct {
			u32 specdata1;
			u32 specdata2;
		} dev;    /* NF4BLK, NF4CHR */
	} u;
	u32		cr_bmval[3];        /* request */
	struct iattr	cr_iattr;           /* request */
	struct nfsd4_change_info  cr_cinfo; /* response */
	struct nfs4_acl *cr_acl;
};
#define cr_linklen	u.link.namelen
#define cr_linkname	u.link.name
#define cr_specdata1	u.dev.specdata1
#define cr_specdata2	u.dev.specdata2

struct nfsd4_delegreturn {
	stateid_t	dr_stateid;
	u32		dr_minorversion;
};

struct nfsd4_getattr {
	u32		ga_bmval[3];        /* request */
	struct svc_fh	*ga_fhp;            /* response */
};

struct nfsd4_link {
	u32		li_namelen;         /* request */
	char *		li_name;            /* request */
	struct nfsd4_change_info  li_cinfo; /* response */
};

struct nfsd4_lock_denied {
	clientid_t	ld_clientid;
	struct nfs4_stateowner   *ld_sop;
	u64             ld_start;
	u64             ld_length;
	u32             ld_type;
};

struct nfsd4_lock {
	/* request */
	u32             lk_type;
	u32             lk_reclaim;         /* boolean */
	u64             lk_offset;
	u64             lk_length;
	u32             lk_is_new;
	union {
		struct {
			u32             open_seqid;
			stateid_t       open_stateid;
			u32             lock_seqid;
			clientid_t      clientid;
			struct xdr_netobj owner;
		} new;
		struct {
			stateid_t       lock_stateid;
			u32             lock_seqid;
		} old;
	} v;

	/* response */
	union {
		struct {
			stateid_t               stateid;
		} ok;
		struct nfsd4_lock_denied        denied;
	} u;
	/* The lk_replay_owner is the open owner in the open_to_lock_owner
	 * case and the lock owner otherwise: */
	struct nfs4_stateowner *lk_replay_owner;
};
#define lk_new_open_seqid       v.new.open_seqid
#define lk_new_open_stateid     v.new.open_stateid
#define lk_new_lock_seqid       v.new.lock_seqid
#define lk_new_clientid         v.new.clientid
#define lk_new_owner            v.new.owner
#define lk_old_lock_stateid     v.old.lock_stateid
#define lk_old_lock_seqid       v.old.lock_seqid

#define lk_rflags       u.ok.rflags
#define lk_resp_stateid u.ok.stateid
#define lk_denied       u.denied


struct nfsd4_lockt {
	u32				lt_type;
	clientid_t			lt_clientid;
	struct xdr_netobj		lt_owner;
	u64				lt_offset;
	u64				lt_length;
	struct nfs4_stateowner * 	lt_stateowner;
	struct nfsd4_lock_denied  	lt_denied;
};

 
struct nfsd4_locku {
	u32             lu_type;
	u32             lu_seqid;
	stateid_t       lu_stateid;
	u64             lu_offset;
	u64             lu_length;
	struct nfs4_stateowner  *lu_stateowner;
};


struct nfsd4_lookup {
	u32		lo_len;             /* request */
	char *		lo_name;            /* request */
};

struct nfsd4_putfh {
	u32		pf_fhlen;           /* request */
	char		*pf_fhval;          /* request */
};

struct nfsd4_open {
	u32		op_claim_type;      /* request */
	struct xdr_netobj op_fname;	    /* request - everything but CLAIM_PREV */
	u32		op_delegate_type;   /* request - CLAIM_PREV only */
	stateid_t       op_delegate_stateid; /* request - response */
	u32		op_create;     	    /* request */
	u32		op_createmode;      /* request */
	u32		op_bmval[3];        /* request */
	struct iattr	iattr;              /* UNCHECKED4, GUARDED4, EXCLUSIVE4_1 */
	nfs4_verifier	verf;               /* EXCLUSIVE4 */
	clientid_t	op_clientid;        /* request */
	struct xdr_netobj op_owner;           /* request */
	u32		op_seqid;           /* request */
	u32		op_share_access;    /* request */
	u32		op_share_deny;      /* request */
	stateid_t	op_stateid;         /* response */
	u32		op_recall;          /* recall */
	struct nfsd4_change_info  op_cinfo; /* response */
	u32		op_rflags;          /* response */
	u32		op_minorversion;    /* used during processing */
	int		op_truncate;        /* used during processing */
	struct nfs4_stateowner *op_stateowner; /* used during processing */
	struct nfs4_acl *op_acl;
};
#define op_iattr	iattr
#define op_verf		verf

struct nfsd4_open_confirm {
	stateid_t	oc_req_stateid		/* request */;
	u32		oc_seqid    		/* request */;
	stateid_t	oc_resp_stateid		/* response */;
	struct nfs4_stateowner * oc_stateowner;	/* response */
};

struct nfsd4_open_downgrade {
	stateid_t       od_stateid;
	u32             od_seqid;
	u32             od_share_access;
	u32             od_share_deny;
	struct nfs4_stateowner *od_stateowner;
};


struct nfsd4_read {
	stateid_t	rd_stateid;         /* request */
	u64		rd_offset;          /* request */
	u32		rd_length;          /* request */
	int		rd_vlen;
	struct file     *rd_filp;
	
	struct svc_rqst *rd_rqstp;          /* response */
	struct svc_fh * rd_fhp;             /* response */
	u32		rd_minorversion;    /* processing */
};

struct nfsd4_readdir {
	u64		rd_cookie;          /* request */
	nfs4_verifier	rd_verf;            /* request */
	u32		rd_dircount;        /* request */
	u32		rd_maxcount;        /* request */
	u32		rd_bmval[3];        /* request */
	struct svc_rqst *rd_rqstp;          /* response */
	struct svc_fh * rd_fhp;             /* response */

	struct readdir_cd	common;
	__be32 *		buffer;
	int			buflen;
	__be32 *		offset;
	u32			minorversion;
};

struct nfsd4_release_lockowner {
	clientid_t        rl_clientid;
	struct xdr_netobj rl_owner;
};
struct nfsd4_readlink {
	struct svc_rqst *rl_rqstp;          /* request */
	struct svc_fh *	rl_fhp;             /* request */
};

struct nfsd4_remove {
	u32		rm_namelen;         /* request */
	char *		rm_name;            /* request */
	struct nfsd4_change_info  rm_cinfo; /* response */
};

struct nfsd4_rename {
	u32		rn_snamelen;        /* request */
	char *		rn_sname;           /* request */
	u32		rn_tnamelen;        /* request */
	char *		rn_tname;           /* request */
	struct nfsd4_change_info  rn_sinfo; /* response */
	struct nfsd4_change_info  rn_tinfo; /* response */
};

struct nfsd4_secinfo {
	u32 si_namelen;					/* request */
	char *si_name;					/* request */
	struct svc_export *si_exp;			/* response */
};

struct nfsd4_setattr {
	stateid_t	sa_stateid;         /* request */
	u32		sa_minorversion;    /* processing */
	u32		sa_bmval[3];        /* request */
	struct iattr	sa_iattr;           /* request */
	struct nfs4_acl *sa_acl;
};

struct nfsd4_setclientid {
	nfs4_verifier	se_verf;            /* request */
	u32		se_namelen;         /* request */
	char *		se_name;            /* request */
	u32		se_callback_prog;   /* request */
	u32		se_callback_netid_len;  /* request */
	char *		se_callback_netid_val;  /* request */
	u32		se_callback_addr_len;   /* request */
	char *		se_callback_addr_val;   /* request */
	u32		se_callback_ident;  /* request */
	clientid_t	se_clientid;        /* response */
	nfs4_verifier	se_confirm;         /* response */
};

struct nfsd4_setclientid_confirm {
	clientid_t	sc_clientid;
	nfs4_verifier	sc_confirm;
};

/* also used for NVERIFY */
struct nfsd4_verify {
	u32		ve_bmval[3];        /* request */
	u32		ve_attrlen;         /* request */
	char *		ve_attrval;         /* request */
	u32		ve_minorversion;
};

struct nfsd4_write {
	stateid_t	wr_stateid;         /* request */
	u64		wr_offset;          /* request */
	u32		wr_stable_how;      /* request */
	u32		wr_buflen;          /* request */
	int		wr_vlen;

	u32		wr_bytes_written;   /* response */
	u32		wr_how_written;     /* response */
	nfs4_verifier	wr_verifier;        /* response */
	u32		wr_minorversion;    /* processing */
};

#if defined(CONFIG_NFSD_V4_1)
struct nfsd4_exchange_id {
	nfs4_verifier	verifier;
	u32		id_len;
	char		*id;
	u32		flags;
	clientid_t	clientid;
	u32		seqid;
};

struct nfsd4_channel {
	u32		headerpadsz;
	u32		maxreq_sz;
	u32		maxresp_sz;
	u32		maxresp_cached;
	u32		maxops;
	u32		maxreqs;
	u32		nr_rdma_attrs;
	u32		rdma_attrs;
};

struct nfsd4_create_session {
	clientid_t		clientid;
	nfs41_sessionid		sessionid;
	u32			seqid;
	u32			flags;
	struct nfsd4_channel	fore_channel;
	struct nfsd4_channel	back_channel;
	u32			callback_prog;
	u32			uid;
	u32			gid;
};

struct nfsd4_sequence {
	nfs41_sessionid		sessionid;		/* request/response */
	u32			seqid;			/* request/response */
	u32			slotid;			/* request/response */
	u32			maxslots;		/* request/response */
	u32			catchthis;		/* request */
	u32			target_maxslots;	/* response */
	u32			status_flags;		/* response */
};

struct nfsd4_destroy_session {
	nfs41_sessionid		sessionid;
};
#endif /* CONFIG_NFSD_V4_1 */

struct nfsd4_op {
	int					opnum;
	__be32					status;
	union {
		struct nfsd4_access		access;
		struct nfsd4_close		close;
		struct nfsd4_commit		commit;
		struct nfsd4_create		create;
		struct nfsd4_delegreturn	delegreturn;
		struct nfsd4_getattr		getattr;
		struct svc_fh *			getfh;
		struct nfsd4_link		link;
		struct nfsd4_lock		lock;
		struct nfsd4_lockt		lockt;
		struct nfsd4_locku		locku;
		struct nfsd4_lookup		lookup;
		struct nfsd4_verify		nverify;
		struct nfsd4_open		open;
		struct nfsd4_open_confirm	open_confirm;
		struct nfsd4_open_downgrade	open_downgrade;
		struct nfsd4_putfh		putfh;
		struct nfsd4_read		read;
		struct nfsd4_readdir		readdir;
		struct nfsd4_readlink		readlink;
		struct nfsd4_remove		remove;
		struct nfsd4_rename		rename;
		clientid_t			renew;
		struct nfsd4_secinfo		secinfo;
		struct nfsd4_setattr		setattr;
		struct nfsd4_setclientid	setclientid;
		struct nfsd4_setclientid_confirm setclientid_confirm;
		struct nfsd4_verify		verify;
		struct nfsd4_write		write;
		struct nfsd4_release_lockowner	release_lockowner;
#if defined(CONFIG_PNFSD)
		struct nfsd4_pnfs_getdevlist	pnfs_getdevlist;
		struct nfsd4_pnfs_getdevinfo	pnfs_getdevinfo;
		struct nfsd4_pnfs_layoutget	pnfs_layoutget;
		struct nfsd4_pnfs_layoutcommit	pnfs_layoutcommit;
		struct nfsd4_pnfs_layoutreturn	pnfs_layoutreturn;
#endif /* CONFIG_PNFSD */
#if defined(CONFIG_NFSD_V4_1)
		struct nfsd4_exchange_id	exchange_id;
		struct nfsd4_create_session	create_session;
		struct nfsd4_sequence		sequence;
		struct nfsd4_destroy_session	destroy_session;
#endif /* CONFIG_NFSD_V4_1 */
	} u;
	struct nfs4_replay *			replay;
};

struct nfsd4_compoundargs {
	/* scratch variables for XDR decode */
	__be32 *			p;
	__be32 *			end;
	struct page **			pagelist;
	int				pagelen;
	__be32				tmp[8];
	__be32 *			tmpp;
	struct tmpbuf {
		struct tmpbuf *next;
		void (*release)(const void *);
		void *buf;
	}				*to_free;

	struct svc_rqst			*rqstp;

	u32				taglen;
	char *				tag;
	u32				minorversion;
	u32				opcnt;
	struct nfsd4_op			*ops;
	struct nfsd4_op			iops[8];
};

struct nfsd4_compoundres {
	/* scratch variables for XDR encode */
	__be32 *			p;
	__be32 *			end;
	struct xdr_buf *		xbuf;
	struct svc_rqst *		rqstp;

	u32				taglen;
	char *				tag;
	u32				opcnt;
	__be32 *			tagp; /* where to encode tag and  opcount */
	u32				minorversion;
};

#define NFS4_SVC_XDRSIZE		sizeof(struct nfsd4_compoundargs)

static inline void
set_change_info(struct nfsd4_change_info *cinfo, struct svc_fh *fhp)
{
	BUG_ON(!fhp->fh_pre_saved || !fhp->fh_post_saved);
	cinfo->atomic = 1;
	cinfo->before_ctime_sec = fhp->fh_pre_ctime.tv_sec;
	cinfo->before_ctime_nsec = fhp->fh_pre_ctime.tv_nsec;
	cinfo->after_ctime_sec = fhp->fh_post_attr.ctime.tv_sec;
	cinfo->after_ctime_nsec = fhp->fh_post_attr.ctime.tv_nsec;
}

int nfs4svc_encode_voidres(struct svc_rqst *, __be32 *, void *);
int nfs4svc_decode_compoundargs(struct svc_rqst *, __be32 *,
		struct nfsd4_compoundargs *);
int nfs4svc_encode_compoundres(struct svc_rqst *, __be32 *,
		struct nfsd4_compoundres *);
void nfsd4_encode_operation(struct nfsd4_compoundres *, struct nfsd4_op *);
void nfsd4_encode_replay(struct nfsd4_compoundres *resp, struct nfsd4_op *op);
__be32 nfsd4_encode_fattr(struct svc_fh *fhp, struct svc_export *exp,
			  struct dentry *dentry, __be32 *buffer, int *countp,
			  u32 *bmval, struct svc_rqst *, int ignore_crossmnt,
			  u32 minorversion);
extern __be32 nfsd4_setclientid(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_setclientid *setclid);
extern __be32 nfsd4_setclientid_confirm(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_setclientid_confirm *setclientid_confirm);
#if defined(CONFIG_NFSD_V4_1)
extern __be32 nfsd4_exchange_id(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_exchange_id *clid);
extern __be32 nfsd4_create_session(struct svc_rqst *,
		struct nfsd4_compound_state *,
		struct nfsd4_create_session *);
extern __be32 nfsd4_sequence(struct svc_rqst *,
		struct nfsd4_compound_state *,
		struct nfsd4_sequence *);
extern __be32 nfsd4_destroy_session(struct svc_rqst *,
		struct nfsd4_compound_state *,
		struct nfsd4_destroy_session *);
#endif /* CONFIG_NFSD_V4_1 */
extern __be32 nfsd4_process_open1(struct nfsd4_open *open);
extern __be32 nfsd4_process_open2(struct svc_rqst *rqstp,
		struct svc_fh *current_fh, struct nfsd4_open *open);
extern __be32 nfsd4_open_confirm(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *, struct nfsd4_open_confirm *oc);
extern __be32 nfsd4_close(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_close *close);
extern __be32 nfsd4_open_downgrade(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_open_downgrade *od);
extern __be32 nfsd4_lock(struct svc_rqst *rqstp, struct nfsd4_compound_state *,
		struct nfsd4_lock *lock);
extern __be32 nfsd4_lockt(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_lockt *lockt);
extern __be32 nfsd4_locku(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_locku *locku);
extern __be32
nfsd4_release_lockowner(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *,
		struct nfsd4_release_lockowner *rlockowner);
extern void nfsd4_release_compoundargs(struct nfsd4_compoundargs *);
extern __be32 nfsd4_delegreturn(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *, struct nfsd4_delegreturn *dr);
extern __be32 nfsd4_renew(struct svc_rqst *rqstp,
			  struct nfsd4_compound_state *, clientid_t *clid);
#if defined(CONFIG_PNFSD)
extern void nfsd4_devlist_free(struct nfsd4_pnfs_getdevlist *gdlp);

extern int filelayout_encode_devaddr(u32 *p, u32 *end, int, void *dev_addr);
extern int filelayout_encode_devinfo(struct pnfs_xdr_info *resp, void *device);
extern void filelayout_free_devaddr(void *devaddr);
extern int filelayout_encode_layout(struct pnfs_xdr_info *resp, void *layout);
extern void filelayout_free_layout(void *layout);
#endif /* CONFIG_PNFSD */

#endif

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
