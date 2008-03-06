/*
 *  fs/nfsd/nfs4proc.c
 *
 *  Server-side procedures for NFSv4.
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
 */

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/file.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/nfs4_acl.h>
#include <linux/sunrpc/gss_api.h>
#if defined(CONFIG_PNFSD)
#include <linux/nfsd/stats.h>
#include <linux/nfsd/pnfsd.h>
#include <linux/exportfs.h>
#endif /* CONFIG_PNFSD */

#define NFSDDBG_FACILITY		NFSDDBG_PROC

#ifdef OP
#error OP is defined
#endif
#define OP(op) [OP_ ## op] = #op
char *nfsd4_op_names[] = {
        OP(ACCESS),
        OP(CLOSE),
        OP(COMMIT),
        OP(CREATE),
        OP(DELEGPURGE),
        OP(DELEGRETURN),
        OP(GETATTR),
        OP(GETFH),
        OP(LINK),
        OP(LOCK),
        OP(LOCKT),
        OP(LOCKU),
        OP(LOOKUP),
        OP(LOOKUPP),
        OP(NVERIFY),
        OP(OPEN),
        OP(OPENATTR),
        OP(OPEN_CONFIRM),
        OP(OPEN_DOWNGRADE),
        OP(PUTFH),
        OP(PUTPUBFH),
        OP(PUTROOTFH),
        OP(READ),
        OP(READDIR),
        OP(READLINK),
        OP(REMOVE),
        OP(RENAME),
        OP(RENEW),
        OP(RESTOREFH),
        OP(SAVEFH),
        OP(SECINFO),
        OP(SETATTR),
        OP(SETCLIENTID),
        OP(SETCLIENTID_CONFIRM),
        OP(VERIFY),
        OP(WRITE),
        OP(RELEASE_LOCKOWNER),
#if defined(CONFIG_NFSD_V4_1)
        OP(EXCHANGE_ID),
        OP(CREATE_SESSION),
        OP(DESTROY_SESSION),
#if defined(CONFIG_PNFSD)
        OP(GETDEVICEINFO),
        OP(GETDEVICELIST),
        OP(LAYOUTCOMMIT),
        OP(LAYOUTGET),
        OP(LAYOUTRETURN),
#endif /* CONFIG_PNFSD */
        OP(SEQUENCE),
#endif /* CONFIG_NFSD_V4_1 */
};
#undef OP

static inline void
fh_dup2(struct svc_fh *dst, struct svc_fh *src)
{
	fh_put(dst);
	dget(src->fh_dentry);
	if (src->fh_export)
		cache_get(&src->fh_export->h);
	*dst = *src;
}

static __be32
do_open_permission(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open, int accmode)
{
	__be32 status;

	if (open->op_truncate &&
		!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;

	if (open->op_share_access & NFS4_SHARE_ACCESS_READ)
		accmode |= MAY_READ;
	if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
		accmode |= (MAY_WRITE | MAY_TRUNC);
	if (open->op_share_deny & NFS4_SHARE_DENY_WRITE)
		accmode |= MAY_WRITE;

	status = fh_verify(rqstp, current_fh, S_IFREG, accmode);

	return status;
}

static __be32
do_open_lookup(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct svc_fh resfh;
	__be32 status;
	int created = 0;

	fh_init(&resfh, NFS4_FHSIZE);
	open->op_truncate = 0;

	if (open->op_create) {
		/*
		 * Note: create modes (UNCHECKED,GUARDED...) are the same
		 * in NFSv4 as in v3.
		 */
		status = nfsd_create_v3(rqstp, current_fh, open->op_fname.data,
					open->op_fname.len, &open->op_iattr,
					&resfh, open->op_createmode,
					(u32 *)open->op_verf.data,
					&open->op_truncate, &created);

		/* If we ever decide to use different attrs to store the
		 * verifier in nfsd_create_v3, then we'll need to change this
		 */
		if (open->op_createmode == NFS4_CREATE_EXCLUSIVE && status == 0)
			open->op_bmval[1] |= (FATTR4_WORD1_TIME_ACCESS |
						FATTR4_WORD1_TIME_MODIFY);
	} else {
		status = nfsd_lookup(rqstp, current_fh,
				     open->op_fname.data, open->op_fname.len, &resfh);
		fh_unlock(current_fh);
	}
	if (status)
		goto out;

	set_change_info(&open->op_cinfo, current_fh);

	/* set reply cache */
	fh_dup2(current_fh, &resfh);
	open->op_stateowner->so_replay.rp_openfh_len = resfh.fh_handle.fh_size;
	memcpy(open->op_stateowner->so_replay.rp_openfh,
			&resfh.fh_handle.fh_base, resfh.fh_handle.fh_size);

	if (!created)
		status = do_open_permission(rqstp, current_fh, open, MAY_NOP);

out:
	fh_put(&resfh);
	return status;
}

static __be32
do_open_fhandle(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	__be32 status;

	/* Only reclaims from previously confirmed clients are valid */
	if ((status = nfs4_check_open_reclaim(&open->op_clientid)))
		return status;

	/* We don't know the target directory, and therefore can not
	* set the change info
	*/

	memset(&open->op_cinfo, 0, sizeof(struct nfsd4_change_info));

	/* set replay cache */
	open->op_stateowner->so_replay.rp_openfh_len = current_fh->fh_handle.fh_size;
	memcpy(open->op_stateowner->so_replay.rp_openfh,
		&current_fh->fh_handle.fh_base,
		current_fh->fh_handle.fh_size);

	open->op_truncate = (open->op_iattr.ia_valid & ATTR_SIZE) &&
		(open->op_iattr.ia_size == 0);

	status = do_open_permission(rqstp, current_fh, open, MAY_OWNER_OVERRIDE);

	return status;
}

#if defined(CONFIG_NFSD_V4_1)
static void
nfsd41_set_clientid(clientid_t *clid, struct current_session *cses)
{
	clid->cl_boot = cses->cs_sid.clientid.cl_boot;
	clid->cl_id = cses->cs_sid.clientid.cl_id;
}
#endif /* CONFIG_NFSD_V4_1 */

static __be32
nfsd4_open(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_open *open, struct nfs4_stateowner **replay_owner)
{
#if defined(CONFIG_NFSD_V4_1)
	struct current_session *current_ses = cstate->current_ses;
#endif
	__be32 status;
	dprintk("NFSD: nfsd4_open filename %.*s op_stateowner %p\n",
		(int)open->op_fname.len, open->op_fname.data,
		open->op_stateowner);

	/* This check required by spec. */
	if (open->op_create && open->op_claim_type != NFS4_OPEN_CLAIM_NULL)
		return nfserr_inval;

#if defined(CONFIG_NFSD_V4_1)
	/* Set the NFSv4.1 client id */
	if (current_ses) {
		nfsd41_set_clientid(&open->op_clientid, current_ses);
		open->op_minorversion = 1;
	} else
		open->op_minorversion = 0;
#endif /* CONFIG_NFSD_V4_1 */
	nfs4_lock_state();

	/* check seqid for replay. set nfs4_owner */
	status = nfsd4_process_open1(open);
	if (status == nfserr_replay_me) {
		struct nfs4_replay *rp = &open->op_stateowner->so_replay;
		fh_put(&cstate->current_fh);
		cstate->current_fh.fh_handle.fh_size = rp->rp_openfh_len;
		memcpy(&cstate->current_fh.fh_handle.fh_base, rp->rp_openfh,
				rp->rp_openfh_len);
		status = fh_verify(rqstp, &cstate->current_fh, 0, MAY_NOP);
		if (status)
			dprintk("nfsd4_open: replay failed"
				" restoring previous filehandle\n");
		else
			status = nfserr_replay_me;
	}
	if (status)
		goto out;

	/* Openowner is now set, so sequence id will get bumped.  Now we need
	 * these checks before we do any creates: */
	status = nfserr_grace;
	if (nfs4_in_grace() && open->op_claim_type != NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;
	status = nfserr_no_grace;
	if (!nfs4_in_grace() && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;

	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_DELEGATE_CUR:
			status = nfserr_inval;
			if (open->op_create)
				goto out;
			/* fall through */
		case NFS4_OPEN_CLAIM_NULL:
			/*
			 * (1) set CURRENT_FH to the file being opened,
			 * creating it if necessary, (2) set open->op_cinfo,
			 * (3) set open->op_truncate if the file is to be
			 * truncated after opening, (4) do permission checking.
			 */
			status = do_open_lookup(rqstp, &cstate->current_fh,
						open);
			if (status)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_PREVIOUS:
			open->op_stateowner->so_confirmed = 1;
			/*
			 * The CURRENT_FH is already set to the file being
			 * opened.  (1) set open->op_cinfo, (2) set
			 * open->op_truncate if the file is to be truncated
			 * after opening, (3) do permission checking.
			*/
			status = do_open_fhandle(rqstp, &cstate->current_fh,
						 open);
			if (status)
				goto out;
			break;
             	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
			open->op_stateowner->so_confirmed = 1;
			dprintk("NFSD: unsupported OPEN claim type %d\n",
				open->op_claim_type);
			status = nfserr_notsupp;
			goto out;
		default:
			dprintk("NFSD: Invalid OPEN claim type %d\n",
				open->op_claim_type);
			status = nfserr_inval;
			goto out;
	}
	/*
	 * nfsd4_process_open2() does the actual opening of the file.  If
	 * successful, it (1) truncates the file if open->op_truncate was
	 * set, (2) sets open->op_stateid, (3) sets open->op_delegation.
	 */
	status = nfsd4_process_open2(rqstp, &cstate->current_fh, open);
out:
	if (open->op_stateowner) {
		nfs4_get_stateowner(open->op_stateowner);
		cstate->replay_owner = open->op_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * filehandle-manipulating ops.
 */
static __be32
nfsd4_getfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct svc_fh **getfh)
{
	if (!cstate->current_fh.fh_dentry)
		return nfserr_nofilehandle;

	*getfh = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_putfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_putfh *putfh)
{
	fh_put(&cstate->current_fh);
	cstate->current_fh.fh_handle.fh_size = putfh->pf_fhlen;
	memcpy(&cstate->current_fh.fh_handle.fh_base, putfh->pf_fhval,
	       putfh->pf_fhlen);
	return fh_verify(rqstp, &cstate->current_fh, 0, MAY_NOP);
}

static __be32
nfsd4_putrootfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		void *arg)
{
	__be32 status;

	fh_put(&cstate->current_fh);
	status = exp_pseudoroot(rqstp, &cstate->current_fh);
	return status;
}

static __be32
nfsd4_restorefh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		void *arg)
{
	if (!cstate->save_fh.fh_dentry)
		return nfserr_restorefh;

	fh_dup2(&cstate->current_fh, &cstate->save_fh);
	return nfs_ok;
}

static __be32
nfsd4_savefh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     void *arg)
{
	if (!cstate->current_fh.fh_dentry)
		return nfserr_nofilehandle;

	fh_dup2(&cstate->save_fh, &cstate->current_fh);
	return nfs_ok;
}

/*
 * misc nfsv4 ops
 */
static __be32
nfsd4_access(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_access *access)
{
	if (access->ac_req_access & ~NFS3_ACCESS_FULL)
		return nfserr_inval;

	access->ac_resp_access = access->ac_req_access;
	return nfsd_access(rqstp, &cstate->current_fh, &access->ac_resp_access,
			   &access->ac_supported);
}

static __be32
nfsd4_commit(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_commit *commit)
{
	__be32 status;
#if defined(CONFIG_PNFSD)
	struct super_block *sb;
	struct svc_fh *current_fh = &cstate->current_fh;
#endif

	u32 *p = (u32 *)commit->co_verf.data;

#if defined(CONFIG_PNFSD)
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (sb->s_export_op->get_verifier) {
		sb->s_export_op->get_verifier(sb, p);
		p += 2;
	} else
#endif /* CONFIG_PNFSD */
	{
	*p++ = nfssvc_boot.tv_sec;
	*p++ = nfssvc_boot.tv_usec;
	}

	status = nfsd_commit(rqstp, &cstate->current_fh, commit->co_offset,
			     commit->co_count);
	if (status == nfserr_symlink)
		status = nfserr_inval;
	return status;
}

static __be32
nfsd4_create(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_create *create)
{
	struct svc_fh resfh;
	__be32 status;
	dev_t rdev;

	fh_init(&resfh, NFS4_FHSIZE);

	status = fh_verify(rqstp, &cstate->current_fh, S_IFDIR, MAY_CREATE);
	if (status == nfserr_symlink)
		status = nfserr_notdir;
	if (status)
		return status;

	switch (create->cr_type) {
	case NF4LNK:
		/* ugh! we have to null-terminate the linktext, or
		 * vfs_symlink() will choke.  it is always safe to
		 * null-terminate by brute force, since at worst we
		 * will overwrite the first byte of the create namelen
		 * in the XDR buffer, which has already been extracted
		 * during XDR decode.
		 */
		create->cr_linkname[create->cr_linklen] = 0;

		status = nfsd_symlink(rqstp, &cstate->current_fh,
				      create->cr_name, create->cr_namelen,
				      create->cr_linkname, create->cr_linklen,
				      &resfh, &create->cr_iattr);
		break;

	case NF4BLK:
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			return nfserr_inval;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFBLK, rdev, &resfh);
		break;

	case NF4CHR:
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			return nfserr_inval;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr,S_IFCHR, rdev, &resfh);
		break;

	case NF4SOCK:
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFSOCK, 0, &resfh);
		break;

	case NF4FIFO:
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFIFO, 0, &resfh);
		break;

	case NF4DIR:
		create->cr_iattr.ia_valid &= ~ATTR_SIZE;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFDIR, 0, &resfh);
		break;

	default:
		status = nfserr_badtype;
	}

	if (!status) {
		fh_unlock(&cstate->current_fh);
		set_change_info(&create->cr_cinfo, &cstate->current_fh);
		fh_dup2(&cstate->current_fh, &resfh);
	}

	fh_put(&resfh);
	return status;
}

static __be32
nfsd4_getattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_getattr *getattr)
{
	__be32 status;

	status = fh_verify(rqstp, &cstate->current_fh, 0, MAY_NOP);
	if (status)
		return status;

	if (getattr->ga_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1)
		return nfserr_inval;

	getattr->ga_bmval[0] &= NFSD_SUPPORTED_ATTRS_WORD0;
	getattr->ga_bmval[1] &= NFSD_SUPPORTED_ATTRS_WORD1;

	getattr->ga_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_link(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_link *link)
{
	__be32 status = nfserr_nofilehandle;

	if (!cstate->save_fh.fh_dentry)
		return status;
	status = nfsd_link(rqstp, &cstate->current_fh,
			   link->li_name, link->li_namelen, &cstate->save_fh);
	if (!status)
		set_change_info(&link->li_cinfo, &cstate->current_fh);
	return status;
}

static __be32
nfsd4_lookupp(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      void *arg)
{
	struct svc_fh tmp_fh;
	__be32 ret;

	fh_init(&tmp_fh, NFS4_FHSIZE);
	ret = exp_pseudoroot(rqstp, &tmp_fh);
	if (ret)
		return ret;
	if (tmp_fh.fh_dentry == cstate->current_fh.fh_dentry) {
		fh_put(&tmp_fh);
		return nfserr_noent;
	}
	fh_put(&tmp_fh);
	return nfsd_lookup(rqstp, &cstate->current_fh,
			   "..", 2, &cstate->current_fh);
}

static __be32
nfsd4_lookup(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_lookup *lookup)
{
	return nfsd_lookup(rqstp, &cstate->current_fh,
			   lookup->lo_name, lookup->lo_len,
			   &cstate->current_fh);
}

static __be32
nfsd4_read(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_read *read)
{
	__be32 status;
	int flags = 0;

	/* no need to check permission - this will be done in nfsd_read() */

	read->rd_filp = NULL;
	if (read->rd_offset >= OFFSET_MAX)
		return nfserr_inval;

	flags = CHECK_FH | RD_STATE;
	if (read->rd_minorversion == 1)
		flags |= NFS_4_1;
	nfs4_lock_state();
	/* check stateid */
	if ((status = nfs4_preprocess_stateid_op(&cstate->current_fh,
				&read->rd_stateid,
				flags, &read->rd_filp))) {
		dprintk("NFSD: nfsd4_read: couldn't process stateid!\n");
		goto out;
	}
	if (read->rd_filp)
		get_file(read->rd_filp);
	status = nfs_ok;
out:
	nfs4_unlock_state();
	read->rd_rqstp = rqstp;
	read->rd_fhp = &cstate->current_fh;
	return status;
}

static __be32
nfsd4_readdir(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_readdir *readdir)
{
	u64 cookie = readdir->rd_cookie;
	static const nfs4_verifier zeroverf;

	/* no need to check permission - this will be done in nfsd_readdir() */

	if (readdir->rd_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1)
		return nfserr_inval;

	readdir->rd_bmval[0] &= NFSD_SUPPORTED_ATTRS_WORD0;
	readdir->rd_bmval[1] &= NFSD_SUPPORTED_ATTRS_WORD1;

	if ((cookie > ~(u32)0) || (cookie == 1) || (cookie == 2) ||
	    (cookie == 0 && memcmp(readdir->rd_verf.data, zeroverf.data, NFS4_VERIFIER_SIZE)))
		return nfserr_bad_cookie;

	readdir->rd_rqstp = rqstp;
	readdir->rd_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_readlink(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	       struct nfsd4_readlink *readlink)
{
	readlink->rl_rqstp = rqstp;
	readlink->rl_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_remove(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_remove *remove)
{
	__be32 status;

	if (nfs4_in_grace())
		return nfserr_grace;
	status = nfsd_unlink(rqstp, &cstate->current_fh, 0,
			     remove->rm_name, remove->rm_namelen);
	if (status == nfserr_symlink)
		return nfserr_notdir;
	if (!status) {
		fh_unlock(&cstate->current_fh);
		set_change_info(&remove->rm_cinfo, &cstate->current_fh);
	}
	return status;
}

static __be32
nfsd4_rename(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_rename *rename)
{
	__be32 status = nfserr_nofilehandle;

	if (!cstate->save_fh.fh_dentry)
		return status;
	if (nfs4_in_grace() && !(cstate->save_fh.fh_export->ex_flags
					& NFSEXP_NOSUBTREECHECK))
		return nfserr_grace;
	status = nfsd_rename(rqstp, &cstate->save_fh, rename->rn_sname,
			     rename->rn_snamelen, &cstate->current_fh,
			     rename->rn_tname, rename->rn_tnamelen);

	/* the underlying filesystem returns different error's than required
	 * by NFSv4. both save_fh and current_fh have been verified.. */
	if (status == nfserr_isdir)
		status = nfserr_exist;
	else if ((status == nfserr_notdir) &&
                  (S_ISDIR(cstate->save_fh.fh_dentry->d_inode->i_mode) &&
                   S_ISDIR(cstate->current_fh.fh_dentry->d_inode->i_mode)))
		status = nfserr_exist;
	else if (status == nfserr_symlink)
		status = nfserr_notdir;

	if (!status) {
		set_change_info(&rename->rn_sinfo, &cstate->current_fh);
		set_change_info(&rename->rn_tinfo, &cstate->save_fh);
	}
	return status;
}

static __be32
nfsd4_secinfo(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_secinfo *secinfo)
{
	struct svc_fh resfh;
	struct svc_export *exp;
	struct dentry *dentry;
	__be32 err;

	fh_init(&resfh, NFS4_FHSIZE);
	err = nfsd_lookup_dentry(rqstp, &cstate->current_fh,
				    secinfo->si_name, secinfo->si_namelen,
				    &exp, &dentry);
	if (err)
		return err;
	if (dentry->d_inode == NULL) {
		exp_put(exp);
		err = nfserr_noent;
	} else
		secinfo->si_exp = exp;
	dput(dentry);
	return err;
}

static __be32
nfsd4_setattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_setattr *setattr)
{
	__be32 status = nfs_ok, flags = 0;

	if (setattr->sa_iattr.ia_valid & ATTR_SIZE) {
		flags = CHECK_FH | WR_STATE;
		if (setattr->sa_minorversion == 1)
			flags |= NFS_4_1;
		nfs4_lock_state();
		status = nfs4_preprocess_stateid_op(&cstate->current_fh,
			&setattr->sa_stateid, flags, NULL);
		nfs4_unlock_state();
		if (status) {
			dprintk("NFSD: nfsd4_setattr: couldn't process stateid!\n");
			return status;
		}
	}
	status = nfs_ok;
	if (setattr->sa_acl != NULL)
		status = nfsd4_set_nfs4_acl(rqstp, &cstate->current_fh,
					    setattr->sa_acl);
	if (status)
		return status;
	status = nfsd_setattr(rqstp, &cstate->current_fh, &setattr->sa_iattr,
				0, (time_t)0);
	return status;
}

static __be32
nfsd4_write(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_write *write)
{
	stateid_t *stateid = &write->wr_stateid;
	struct file *filp = NULL;
	u32 *p;
	__be32 status = nfs_ok, flags = 0;
#if defined(CONFIG_PNFSD)
	struct super_block *sb;
	struct svc_fh *current_fh = &cstate->current_fh;
#endif

	/* no need to check permission - this will be done in nfsd_write() */

	if (write->wr_offset >= OFFSET_MAX)
		return nfserr_inval;

	flags = CHECK_FH | WR_STATE;
	if (write->wr_minorversion == 1)
		flags |= NFS_4_1;
	nfs4_lock_state();
	status = nfs4_preprocess_stateid_op(&cstate->current_fh, stateid,
					flags, &filp);
	if (filp)
		get_file(filp);
	nfs4_unlock_state();

	if (status) {
		dprintk("NFSD: nfsd4_write: couldn't process stateid!\n");
		return status;
	}

	write->wr_bytes_written = write->wr_buflen;
	write->wr_how_written = write->wr_stable_how;
	p = (u32 *)write->wr_verifier.data;

#if defined(CONFIG_PNFSD)
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (sb->s_export_op->get_verifier) {
		struct pnfs_ds_stateid *dsp = find_pnfs_ds_stateid(stateid);
		if (dsp) {
			/* get it from MDS */
			*p++ = dsp->ds_verifier[0];
			*p++ = dsp->ds_verifier[1];
		} else {
			/* must be on MDS */
			sb->s_export_op->get_verifier(sb, p);
			p += 2;
		}
	} else
#endif /* CONFIG_PNFSD */
	{
	*p++ = nfssvc_boot.tv_sec;
	*p++ = nfssvc_boot.tv_usec;
	}
	status =  nfsd_write(rqstp, &cstate->current_fh, filp,
			     write->wr_offset, rqstp->rq_vec, write->wr_vlen,
			     write->wr_buflen, &write->wr_how_written);
	if (filp)
		fput(filp);

	if (status == nfserr_symlink)
		status = nfserr_inval;
	return status;
}

/* This routine never returns NFS_OK!  If there are no other errors, it
 * will return NFSERR_SAME or NFSERR_NOT_SAME depending on whether the
 * attributes matched.  VERIFY is implemented by mapping NFSERR_SAME
 * to NFS_OK after the call; NVERIFY by mapping NFSERR_NOT_SAME to NFS_OK.
 */
static __be32
_nfsd4_verify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_verify *verify)
{
	__be32 *buf, *p;
	int count;
	__be32 status;

	status = fh_verify(rqstp, &cstate->current_fh, 0, MAY_NOP);
	if (status)
		return status;

	if ((verify->ve_bmval[0] & ~NFSD_SUPPORTED_ATTRS_WORD0)
	    || (verify->ve_bmval[1] & ~NFSD_SUPPORTED_ATTRS_WORD1))
		return nfserr_attrnotsupp;
	if ((verify->ve_bmval[0] & FATTR4_WORD0_RDATTR_ERROR)
	    || (verify->ve_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1))
		return nfserr_inval;
	if (verify->ve_attrlen & 3)
		return nfserr_inval;

	/* count in words:
	 *   bitmap_len(1) + bitmap(2) + attr_len(1) = 4
	 */
	count = 4 + (verify->ve_attrlen >> 2);
	buf = kmalloc(count << 2, GFP_KERNEL);
	if (!buf)
		return nfserr_resource;

	status = nfsd4_encode_fattr(&cstate->current_fh,
				    cstate->current_fh.fh_export,
				    cstate->current_fh.fh_dentry, buf,
				    &count, verify->ve_bmval,
				    rqstp);

	/* this means that nfsd4_encode_fattr() ran out of space */
	if (status == nfserr_resource && count == 0)
		status = nfserr_not_same;
	if (status)
		goto out_kfree;

	p = buf + 3;
	status = nfserr_not_same;
	if (ntohl(*p++) != verify->ve_attrlen)
		goto out_kfree;
	if (!memcmp(p, verify->ve_attrval, verify->ve_attrlen))
		status = nfserr_same;

out_kfree:
	kfree(buf);
	return status;
}

static __be32
nfsd4_nverify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_verify *verify)
{
	__be32 status;

	status = _nfsd4_verify(rqstp, cstate, verify);
	return status == nfserr_not_same ? nfs_ok : status;
}

static __be32
nfsd4_verify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_verify *verify)
{
	__be32 status;

	status = _nfsd4_verify(rqstp, cstate, verify);
	return status == nfserr_same ? nfs_ok : status;
}

#if defined(CONFIG_PNFSD)
static __be32
nfsd4_getdevlist(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_pnfs_getdevlist *gdlp)
{
	struct super_block *sb;
	struct svc_fh *current_fh = &cstate->current_fh;
	int status = 0;

	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status) {
		printk("pNFS %s: verify filehandle failed\n", __FUNCTION__);
		goto out;
	}

	status = nfserr_inval;
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (!sb)
		goto out;

	/* check to see if requested layout type is supported. */
	status = nfserr_unknown_layouttype;
	if (!sb->s_export_op->layout_type ||
	    sb->s_export_op->layout_type() != gdlp->gd_type) {
		printk("pNFS %s: requested layout type %d "
		       "does not match suppored type %d\n",
			__FUNCTION__, gdlp->gd_type,
			sb->s_export_op->layout_type());
		goto out;
	}

	/* set the layouttype for encoding the devaddr */
	gdlp->gd_ops = sb->s_export_op;

	/* device list is allocated by underlying file system, and free'd
	 * via export_ops callback. */
	if (sb->s_export_op->get_devicelist) {
		status = sb->s_export_op->get_devicelist(sb, (void *)gdlp);

		dprintk("%s: status %d type %d maxcount %d len %d\n",
			__FUNCTION__, status, gdlp->gd_type, gdlp->gd_maxcount,
			gdlp->gd_devlist_len);
	}
	if (gdlp->gd_devlist_len < 0)
		status = nfserr_inval;
out:
	return status;
}

/*
 * NOTE: to implement CB_LAYOUTRECALL, need to associate layouts with clientid
 */
static __be32
nfsd4_layoutget(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_pnfs_layoutget *lgp)
{
	int status = 0;
	struct super_block *sb;
	struct current_session *current_ses = cstate->current_ses;
	struct svc_fh *current_fh = &cstate->current_fh;

	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status) {
		printk("pNFS %s: verify filehandle failed\n", __FUNCTION__);
		goto out;
	}

	status = nfserr_inval;
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (!sb)
		goto out;

	/* check to see if requested layout type is supported. */
	status = nfserr_unknown_layouttype;
	if (!sb->s_export_op->layout_type ||
	    sb->s_export_op->layout_type() != lgp->lg_seg.layout_type) {
		printk("pNFS %s: requested layout type %d "
		       "does not match suppored type %d\n",
			__FUNCTION__, lgp->lg_seg.layout_type,
			sb->s_export_op->layout_type());
		goto out;
	}

	status = nfserr_layoutunavailable;
	if (!sb->s_export_op->layout_get) {
		dprintk("pNFS %s: layout_get not implemented for layout "
			"type %d\n", __FUNCTION__, lgp->lg_seg.layout_type);
		goto out;
	}

	status = nfserr_inval;
	if (lgp->lg_seg.iomode != IOMODE_READ &&
	    lgp->lg_seg.iomode != IOMODE_RW &&
	    lgp->lg_seg.iomode != IOMODE_ANY) {
		dprintk("pNFS %s: invalid iomode %d\n", __FUNCTION__,
			lgp->lg_seg.iomode);
		goto out;
	}

	status = nfserr_badiomode;
	if (lgp->lg_seg.iomode == IOMODE_ANY) {
		dprintk("pNFS %s: IOMODE_ANY is not allowed\n", __FUNCTION__);
		goto out;
	}

	/* set the export ops for encoding the devaddr */
	lgp->lg_ops = sb->s_export_op;

	lgp->lg_seg.clientid = *(u64 *)&current_ses->cs_sid.clientid;

	status = nfs4_pnfs_get_layout(sb, current_fh, lgp);
out:
	return status;
}

static __be32
nfsd4_layoutcommit(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_pnfs_layoutcommit *lcp)
{
	int status;
	struct inode *ino = NULL;
	struct iattr ia;
	struct super_block *sb;
	struct current_session *current_ses = cstate->current_ses;
	struct svc_fh *current_fh = &cstate->current_fh;

	dprintk("NFSD: nfsd4_layoutcommit \n");
	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status) {
		printk("%s: verify filehandle failed\n", __FUNCTION__);
		goto out;
	}

	ino = current_fh->fh_dentry->d_inode;

	/* This will only extend the file length.  Do a quick
	 * check to see if there is any point in waiting for the update
	 * locks.
	 * TODO: Is this correct for all back ends?
	 */
	dprintk("%s:new size: %llu old size: %lld\n",
		__FUNCTION__, lcp->lc_last_wr + 1, ino->i_size);

	fh_lock(current_fh);
	if ((lcp->lc_last_wr + 1) <= ino->i_size) {
		status = 0;
		lcp->lc_size_chg = 0;
		fh_unlock(current_fh);
		goto out;
	}

	/* Set clientid from sessionid */
	lcp->lc_seg.clientid = *(u64 *)&current_ses->cs_sid.clientid;

	/* Try our best to update the file size */
	dprintk("%s: Modifying file size\n", __FUNCTION__);
	ia.ia_valid = ATTR_SIZE;
	ia.ia_size = lcp->lc_last_wr + 1;
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (sb->s_export_op->layout_commit) {
		status = sb->s_export_op->layout_commit(ino, lcp);
		dprintk("%s:layout_commit result %d\n", __FUNCTION__, status);
	} else {
		status = notify_change(current_fh->fh_dentry, &ia);
		dprintk("%s:notify_change result %d\n", __FUNCTION__, status);
	}

	fh_unlock(current_fh);

	if (!status) {
		if (EX_ISSYNC(current_fh->fh_export)) {
			dprintk("%s:Synchronously writing inode size %llu\n",
				__FUNCTION__, ino->i_size);
			write_inode_now(ino, 1);
		}
		lcp->lc_size_chg = 1;
		lcp->lc_newsize = ino->i_size;
		status = 0;
	}
out:
	return status;
}

static __be32
nfsd4_layoutreturn(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_pnfs_layoutreturn *lrp)
{
	int status = 0;
	struct super_block *sb;
	struct current_session *current_ses = cstate->current_ses;
	struct svc_fh *current_fh = &cstate->current_fh;

	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status) {
		printk("pNFS %s: verify filehandle failed\n", __FUNCTION__);
		goto out;
	}

	status = nfserr_inval;
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (!sb)
		goto out;

	/* check to see if requested layout type is supported. */
	status = nfserr_unknown_layouttype;
	if (!sb->s_export_op->layout_type ||
	    sb->s_export_op->layout_type() != lrp->lr_seg.layout_type) {
		printk("pNFS %s: requested layout type %d "
		       "does not match suppored type %d\n",
			__FUNCTION__, lrp->lr_seg.layout_type,
			sb->s_export_op->layout_type());
		goto out;
	}

	/* Set clientid from sessionid */
	lrp->lr_seg.clientid = *(u64 *)&current_ses->cs_sid.clientid;
	status = nfs4_pnfs_return_layout(sb, current_fh, lrp);
out:
	dprintk("pNFS %s: status %d layout_type 0x%x\n",
		__FUNCTION__, status, lrp->lr_seg.layout_type);
	return status;
}

static __be32
nfsd4_getdevinfo(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_pnfs_getdevinfo *gdp)
{
	struct super_block *sb;
	struct svc_fh *current_fh = &cstate->current_fh;
	int status;

	printk("%s: type %d dev_id %d\n",
		__FUNCTION__, gdp->gd_type, gdp->gd_dev_id);

	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status) {
		printk("%s: verify filehandle failed\n", __FUNCTION__);
		goto out;
	}

	status = nfserr_inval;
	sb = current_fh->fh_dentry->d_inode->i_sb;
	if (!sb)
		goto out;

	/* check to see if requested layout type is supported. */
	status = nfserr_unknown_layouttype;
	if (!sb->s_export_op->layout_type ||
	    sb->s_export_op->layout_type() != gdp->gd_type) {
		printk("pNFS %s: requested layout type %d "
		       "does not match suppored type %d\n",
			__FUNCTION__, gdp->gd_type,
			sb->s_export_op->layout_type());
		goto out;
	}

	/* set the ops for encoding the devaddr */
	gdp->gd_ops = sb->s_export_op;

	if (sb && sb->s_export_op->get_deviceinfo) {
		status = sb->s_export_op->get_deviceinfo(sb, (void *)gdp);

		dprintk("%s: status %d type %d dev_id %d\n",
			__FUNCTION__, status, gdp->gd_type, gdp->gd_dev_id);
		goto out;
	}
	printk("%s:  failed, no support\n", __FUNCTION__);
out:
	return status;
}
#endif /* CONFIG_PNFSD */

/*
 * NULL call.
 */
static __be32
nfsd4_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return nfs_ok;
}

static inline void nfsd4_increment_op_stats(u32 opnum)
{
	if (opnum >= FIRST_NFS4_OP && opnum <= LAST_NFS4_OP)
		nfsdstats.nfs4_opcount[opnum]++;
}

static void cstate_free(struct nfsd4_compound_state *cstate)
{
	if (cstate == NULL)
		return;
	fh_put(&cstate->current_fh);
	fh_put(&cstate->save_fh);
	BUG_ON(cstate->replay_owner);
	kfree(cstate);
}

static struct nfsd4_compound_state *cstate_alloc(void)
{
	struct nfsd4_compound_state *cstate;

	cstate = kmalloc(sizeof(struct nfsd4_compound_state), GFP_KERNEL);
	if (cstate == NULL)
		return NULL;
	fh_init(&cstate->current_fh, NFS4_FHSIZE);
	fh_init(&cstate->save_fh, NFS4_FHSIZE);
	cstate->replay_owner = NULL;
	return cstate;
}

typedef __be32(*nfsd4op_func)(struct svc_rqst *, struct nfsd4_compound_state *,
			      void *);

struct nfsd4_operation {
	nfsd4op_func op_func;
	u32 op_flags;
/* Most ops require a valid current filehandle; a few don't: */
#define ALLOWED_WITHOUT_FH 1
/* GETATTR and ops not listed as returning NFS4ERR_MOVED: */
#define ALLOWED_ON_ABSENT_FS 2
};

static struct nfsd4_operation nfsd4_ops[];

/*
 * COMPOUND call.
 */
static __be32
nfsd4_proc_compound(struct svc_rqst *rqstp,
		    struct nfsd4_compoundargs *args,
		    struct nfsd4_compoundres *resp)
{
	struct nfsd4_op	*op = NULL;
	struct nfsd4_operation *opdesc;
	struct nfsd4_compound_state *cstate = NULL;
	int		slack_bytes;
	__be32		status;
#if defined(CONFIG_NFSD_V4_1)
	struct current_session *current_ses = NULL;
#endif /* CONFIG_NFSD_V4_1 */

	status = nfserr_resource;
	cstate = cstate_alloc();
	if (cstate == NULL)
		goto out;

#if defined(CONFIG_NFSD_V4_1)
	if (args->minorversion == 1) {
		current_ses = kzalloc(sizeof(*current_ses), GFP_KERNEL);
		if (current_ses == NULL)
			goto out;
	}
	/* DM: current_ses must be NULL for minorversion 0 */
	cstate->current_ses = current_ses;
#endif /* CONFIG_NFSD_V4_1 */

	resp->xbuf = &rqstp->rq_res;
	resp->p = rqstp->rq_res.head[0].iov_base + rqstp->rq_res.head[0].iov_len;
	resp->tagp = resp->p;
	/* reserve space for: taglen, tag, and opcnt */
	resp->p += 2 + XDR_QUADLEN(args->taglen);
	resp->end = rqstp->rq_res.head[0].iov_base + PAGE_SIZE;
	resp->taglen = args->taglen;
	resp->tag = args->tag;
	resp->opcnt = 0;
	resp->rqstp = rqstp;

	/*
	 * According to RFC3010, this takes precedence over all other errors.
	 */
	status = nfserr_minor_vers_mismatch;
	if (args->minorversion > NFSD_SUPPORTED_MINOR_VERSION)
		goto out;

	status = nfs_ok;
	while (!status && resp->opcnt < args->opcnt) {
		op = &args->ops[resp->opcnt++];

		dprintk("nfsv4 compound op %p opcnt %d #%d: %d\n",
			args->ops, args->opcnt, resp->opcnt, op->opnum);

		/*
		 * The XDR decode routines may have pre-set op->status;
		 * for example, if there is a miscellaneous XDR error
		 * it will be set to nfserr_bad_xdr.
		 */
		if (op->status)
			goto encode_op;

		/* We must be able to encode a successful response to
		 * this operation, with enough room left over to encode a
		 * failed response to the next operation.  If we don't
		 * have enough room, fail with ERR_RESOURCE.
		 */
		slack_bytes = (char *)resp->end - (char *)resp->p;
		if (slack_bytes < COMPOUND_SLACK_SPACE
				+ COMPOUND_ERR_SLACK_SPACE) {
			BUG_ON(slack_bytes < COMPOUND_ERR_SLACK_SPACE);
			op->status = nfserr_resource;
			goto encode_op;
		}

		opdesc = &nfsd4_ops[op->opnum];

		if (!cstate->current_fh.fh_dentry) {
			if (!(opdesc->op_flags & ALLOWED_WITHOUT_FH)) {
				op->status = nfserr_nofilehandle;
				goto encode_op;
			}
		} else if (cstate->current_fh.fh_export->ex_fslocs.migrated &&
			  !(opdesc->op_flags & ALLOWED_ON_ABSENT_FS)) {
			op->status = nfserr_moved;
			goto encode_op;
		}

#if defined(CONFIG_NFSD_V4_1)
		if ((args->minorversion == 1) &&
		    ((op->opnum == OP_SETCLIENTID) ||
		    (op->opnum == OP_SETCLIENTID_CONFIRM) ||
		    (op->opnum == OP_OPEN_CONFIRM) ||
		    (op->opnum == OP_RELEASE_LOCKOWNER) ||
		    (op->opnum == OP_RENEW))) {
			op->status = nfserr_notsupp;
			goto encode_op;
		}
#endif /* CONFIG_NFSD_V4_1 */
		dprintk("xxx server proc %2d %s\n", op->opnum,
			op->opnum < ARRAY_SIZE(nfsd4_op_names) &&
			nfsd4_op_names[op->opnum] ? nfsd4_op_names[op->opnum] : "");

		if (opdesc->op_func)
			op->status = opdesc->op_func(rqstp, cstate, &op->u);
		else
			BUG_ON(op->status == nfs_ok);

encode_op:
		if (op->status == nfserr_replay_me) {
			op->replay = &cstate->replay_owner->so_replay;
			nfsd4_encode_replay(resp, op);
			status = op->status = op->replay->rp_status;
		} else {
			nfsd4_encode_operation(resp, op);
			status = op->status;
		}

		dprintk("nfsv4 compound op %p opcnt %d #%d: %d: status %d\n",
			args->ops, args->opcnt, resp->opcnt, op->opnum,
			be32_to_cpu(status));

		if (cstate->replay_owner) {
			nfs4_put_stateowner(cstate->replay_owner);
			cstate->replay_owner = NULL;
		}
		/* XXX Ugh, we need to get rid of this kind of special case: */
		if (op->opnum == OP_READ && op->u.read.rd_filp)
			fput(op->u.read.rd_filp);

		nfsd4_increment_op_stats(op->opnum);
	}

out:
#if defined(CONFIG_NFSD_V4_1)
	if (cstate->current_ses) {
		if (cstate->current_ses->cs_slot) {
			if (op && op->status != nfserr_dropit) {
				dprintk("%s SET SLOT STATE TO AVAILABLE\n",
								__FUNCTION__);
				nfs41_set_slot_state(current_ses->cs_slot,
							NFS4_SLOT_AVAILABLE);
			}
			nfs41_put_session(cstate->current_ses->cs_slot->sl_session);
		}
		kfree(cstate->current_ses);
	}
#endif /* CONFIG_NFSD_V4_1 */
	cstate_free(cstate);
	return status;
}

#if defined(CONFIG_NFSD_V4_1)
#define NFSD4_LAST_OP OP_SEQUENCE
#else /* CONFIG_NFSD_V4_1 */
#define NFSD4_LAST_OP OP_RELEASE_LOCKOWNER
#endif /* CONFIG_NFSD_V4_1 */

static struct nfsd4_operation nfsd4_ops[NFSD4_LAST_OP+1] = {
	[OP_ACCESS] = {
		.op_func = (nfsd4op_func)nfsd4_access,
	},
	[OP_CLOSE] = {
		.op_func = (nfsd4op_func)nfsd4_close,
	},
	[OP_COMMIT] = {
		.op_func = (nfsd4op_func)nfsd4_commit,
	},
	[OP_CREATE] = {
		.op_func = (nfsd4op_func)nfsd4_create,
	},
	[OP_DELEGRETURN] = {
		.op_func = (nfsd4op_func)nfsd4_delegreturn,
	},
	[OP_GETATTR] = {
		.op_func = (nfsd4op_func)nfsd4_getattr,
		.op_flags = ALLOWED_ON_ABSENT_FS,
	},
	[OP_GETFH] = {
		.op_func = (nfsd4op_func)nfsd4_getfh,
	},
	[OP_LINK] = {
		.op_func = (nfsd4op_func)nfsd4_link,
	},
	[OP_LOCK] = {
		.op_func = (nfsd4op_func)nfsd4_lock,
	},
	[OP_LOCKT] = {
		.op_func = (nfsd4op_func)nfsd4_lockt,
	},
	[OP_LOCKU] = {
		.op_func = (nfsd4op_func)nfsd4_locku,
	},
	[OP_LOOKUP] = {
		.op_func = (nfsd4op_func)nfsd4_lookup,
	},
	[OP_LOOKUPP] = {
		.op_func = (nfsd4op_func)nfsd4_lookupp,
	},
	[OP_NVERIFY] = {
		.op_func = (nfsd4op_func)nfsd4_nverify,
	},
	[OP_OPEN] = {
		.op_func = (nfsd4op_func)nfsd4_open,
	},
	[OP_OPEN_CONFIRM] = {
		.op_func = (nfsd4op_func)nfsd4_open_confirm,
	},
	[OP_OPEN_DOWNGRADE] = {
		.op_func = (nfsd4op_func)nfsd4_open_downgrade,
	},
	[OP_PUTFH] = {
		.op_func = (nfsd4op_func)nfsd4_putfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_PUTPUBFH] = {
		/* unsupported; just for future reference: */
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_PUTROOTFH] = {
		.op_func = (nfsd4op_func)nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_READ] = {
		.op_func = (nfsd4op_func)nfsd4_read,
	},
	[OP_READDIR] = {
		.op_func = (nfsd4op_func)nfsd4_readdir,
	},
	[OP_READLINK] = {
		.op_func = (nfsd4op_func)nfsd4_readlink,
	},
	[OP_REMOVE] = {
		.op_func = (nfsd4op_func)nfsd4_remove,
	},
	[OP_RENAME] = {
		.op_func = (nfsd4op_func)nfsd4_rename,
	},
	[OP_RENEW] = {
		.op_func = (nfsd4op_func)nfsd4_renew,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_RESTOREFH] = {
		.op_func = (nfsd4op_func)nfsd4_restorefh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_SAVEFH] = {
		.op_func = (nfsd4op_func)nfsd4_savefh,
	},
	[OP_SECINFO] = {
		.op_func = (nfsd4op_func)nfsd4_secinfo,
	},
	[OP_SETATTR] = {
		.op_func = (nfsd4op_func)nfsd4_setattr,
	},
	[OP_SETCLIENTID] = {
		.op_func = (nfsd4op_func)nfsd4_setclientid,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_SETCLIENTID_CONFIRM] = {
		.op_func = (nfsd4op_func)nfsd4_setclientid_confirm,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
	[OP_VERIFY] = {
		.op_func = (nfsd4op_func)nfsd4_verify,
	},
	[OP_WRITE] = {
		.op_func = (nfsd4op_func)nfsd4_write,
	},
	[OP_RELEASE_LOCKOWNER] = {
		.op_func = (nfsd4op_func)nfsd4_release_lockowner,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS,
	},
#if defined(CONFIG_NFSD_V4_1)
#if defined(CONFIG_PNFSD)
	[OP_GETDEVICELIST] = {
		.op_func = (nfsd4op_func)nfsd4_getdevlist,
		.op_flags = ALLOWED_WITHOUT_FH,
	},
	[OP_GETDEVICEINFO] = {
		.op_func = (nfsd4op_func)nfsd4_getdevinfo,
		.op_flags = ALLOWED_WITHOUT_FH,
	},
	[OP_LAYOUTGET] = {
		.op_func = (nfsd4op_func)nfsd4_layoutget,
	},
	[OP_LAYOUTCOMMIT] = {
		.op_func = (nfsd4op_func)nfsd4_layoutcommit,
	},
	[OP_LAYOUTRETURN] = {
		.op_func = (nfsd4op_func)nfsd4_layoutreturn,
	},
#endif /* CONFIG_PNFSD */
	[OP_EXCHANGE_ID] = {
		.op_func = (nfsd4op_func)nfsd4_exchange_id,
		.op_flags = ALLOWED_WITHOUT_FH,
	},
	[OP_CREATE_SESSION] = {
		.op_func = (nfsd4op_func)nfsd4_create_session,
		.op_flags = ALLOWED_WITHOUT_FH,
	},
	[OP_SEQUENCE] = {
		.op_func = (nfsd4op_func)nfsd4_sequence,
		.op_flags = ALLOWED_WITHOUT_FH,
	},
	[OP_DESTROY_SESSION] = {
		.op_func = (nfsd4op_func)nfsd4_destroy_session,
		.op_flags = ALLOWED_WITHOUT_FH,
	},
#endif /* CONFIG_NFSD_V4_1 */
};

#define nfs4svc_decode_voidargs		NULL
#define nfs4svc_release_void		NULL
#define nfsd4_voidres			nfsd4_voidargs
#define nfs4svc_release_compound	NULL
struct nfsd4_voidargs { int dummy; };

#define PROC(name, argt, rest, relt, cache, respsize)	\
 { (svc_procfunc) nfsd4_proc_##name,		\
   (kxdrproc_t) nfs4svc_decode_##argt##args,	\
   (kxdrproc_t) nfs4svc_encode_##rest##res,	\
   (kxdrproc_t) nfs4svc_release_##relt,		\
   sizeof(struct nfsd4_##argt##args),		\
   sizeof(struct nfsd4_##rest##res),		\
   0,						\
   cache,					\
   respsize,					\
 }

/*
 * TODO: At the present time, the NFSv4 server does not do XID caching
 * of requests.  Implementing XID caching would not be a serious problem,
 * although it would require a mild change in interfaces since one
 * doesn't know whether an NFSv4 request is idempotent until after the
 * XDR decode.  However, XID caching totally confuses pynfs (Peter
 * Astrand's regression testsuite for NFSv4 servers), which reuses
 * XID's liberally, so I've left it unimplemented until pynfs generates
 * better XID's.
 */
static struct svc_procedure		nfsd_procedures4[2] = {
  PROC(null,	 void,		void,		void,	  RC_NOCACHE, 1),
  PROC(compound, compound,	compound,	compound, RC_NOCACHE, NFSD_BUFSIZE/4)
};

struct svc_version	nfsd_version4 = {
		.vs_vers	= 4,
		.vs_nproc	= 2,
		.vs_proc	= nfsd_procedures4,
		.vs_dispatch	= nfsd_dispatch,
		.vs_xdrsize	= NFS4_SVC_XDRSIZE,
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
