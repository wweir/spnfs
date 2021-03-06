/*
 *  linux/fs/nfsd/nfs4callback.c
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
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
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/inet.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/state.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs4.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

#define NFSPROC4_CB_NULL 0
#define NFSPROC4_CB_COMPOUND 1
#define NFS4_STATEID_SIZE 16

#if defined(CONFIG_NFSD_V4_1)
#define NFS4_CB_PROGRAM 0x40000000
#endif

/* Index of predefined Linux callback client operations */

enum {
	NFSPROC4_CLNT_CB_NULL = 0,
	NFSPROC4_CLNT_CB_RECALL,
	NFSPROC4_CLNT_CB_SEQUENCE,
#if defined(CONFIG_PNFSD)
	NFSPROC4_CLNT_CB_LAYOUT,
	NFSPROC4_CLNT_CB_DEVICE,
#endif
};

enum nfs_cb_opnum4 {
	OP_CB_RECALL            = 4,
	OP_CB_LAYOUT            = 5,
	OP_CB_SEQUENCE          = 11,
	OP_CB_DEVICE            = 14,
};

#define NFS4_MAXTAGLEN		20

#define NFS4_enc_cb_null_sz		0
#define NFS4_dec_cb_null_sz		0
#define cb_compound_enc_hdr_sz		4
#define cb_compound_dec_hdr_sz		(3 + (NFS4_MAXTAGLEN >> 2))
#define op_enc_sz			1
#define op_dec_sz			2
#define enc_nfs4_fh_sz			(1 + (NFS4_FHSIZE >> 2))
#define enc_stateid_sz			(NFS4_STATEID_SIZE >> 2)
#define NFS4_enc_cb_recall_sz		(cb_compound_enc_hdr_sz +       \
					1 + enc_stateid_sz +            \
					enc_nfs4_fh_sz)
#define NFS4_dec_cb_recall_sz		(cb_compound_dec_hdr_sz  +      \
					op_dec_sz)

#if defined(CONFIG_NFSD_V4_1)
#define NFS41_enc_cb_null_sz		0
#define NFS41_dec_cb_null_sz		0
#define cb_compound41_enc_hdr_sz	4
#define cb_compound41_dec_hdr_sz	(3 + (NFS4_MAXTAGLEN >> 2))
#define sessionid_sz			(NFS4_MAX_SESSIONID_LEN >> 2)
#define cb_sequence41_enc_sz		(sessionid_sz + 4 +             \
					1 /* no referring calls list yet */)
#define cb_sequence41_dec_sz		(op_dec_sz + sessionid_sz + 4)
#define NFS41_enc_cb_recall_sz		(cb_compound41_enc_hdr_sz +     \
					cb_sequence41_enc_sz +          \
					1 + enc_stateid_sz +            \
					enc_nfs4_fh_sz)
#define NFS41_dec_cb_recall_sz		(cb_compound_dec_hdr_sz  +      \
					cb_sequence41_dec_sz +          \
					op_dec_sz)
#define NFS41_enc_cb_layout_sz		(cb_compound_enc_hdr_sz +       \
					cb_sequence41_enc_sz +          \
					1 + 3 +                         \
					enc_nfs4_fh_sz + 4)
#define NFS41_dec_cb_layout_sz		(cb_compound_dec_hdr_sz  +      \
					cb_sequence41_dec_sz +          \
					op_dec_sz)
#define NFS41_enc_cb_device_sz		(cb_compound_enc_hdr_sz +       \
					cb_sequence41_enc_sz +          \
					1 + 6)
#define NFS41_dec_cb_device_sz		(cb_compound_dec_hdr_sz  +      \
					cb_sequence41_dec_sz +          \
					op_dec_sz)

struct nfs41_rpc_args {
	struct nfs4_callback     *args_callback;
	void                     *args_op;
	struct nfs41_cb_sequence *args_seq;
};

struct nfs41_rpc_res {
	struct nfs4_callback     *res_callback;
	void                     *res_op;
	struct nfs41_cb_sequence *res_seq;
};
#endif /* defined(CONFIG_NFSD_V4_1) */

/*
* Generic encode routines from fs/nfs/nfs4xdr.c
*/
static inline __be32 *
xdr_writemem(__be32 *p, const void *ptr, int nbytes)
{
	int tmp = XDR_QUADLEN(nbytes);
	if (!tmp)
		return p;
	p[tmp-1] = 0;
	memcpy(p, ptr, nbytes);
	return p + tmp;
}

#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((u32)((n) >> 32));				\
	*p++ = htonl((u32)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {                           \
	p = xdr_writemem(p, ptr, nbytes);                       \
} while (0)
#define RESERVE_SPACE(nbytes)   do {                            \
	p = xdr_reserve_space(xdr, nbytes);                     \
	if (!p) dprintk("NFSD: RESERVE_SPACE(%d) failed in function %s\n", (int) (nbytes), __FUNCTION__); \
	BUG_ON(!p);                                             \
} while (0)

/*
 * Generic decode routines from fs/nfs/nfs4xdr.c
 */
#define DECODE_TAIL                             \
	status = 0;                             \
out:                                            \
	return status;                          \
xdr_error:                                      \
	dprintk("NFSD: xdr error! (%s:%d)\n", __FILE__, __LINE__); \
	status = -EIO;                          \
	goto out

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {                  \
	(x) = (u64)ntohl(*p++) << 32;           \
	(x) |= ntohl(*p++);                     \
} while (0)
#define READTIME(x)       do {                  \
	p++;                                    \
	(x.tv_sec) = ntohl(*p++);               \
	(x.tv_nsec) = ntohl(*p++);              \
} while (0)
#define READ_BUF(nbytes)  do { \
	p = xdr_inline_decode(xdr, nbytes); \
	if (!p) { \
		dprintk("NFSD: %s: reply buffer overflowed in line %d.\n", \
			__FUNCTION__, __LINE__); \
		return -EIO; \
	} \
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

struct nfs4_cb_compound_hdr {
	/* args */
	u32		ident;		/* minorversion 0 only */
	u32		nops;

	/* res */
	int		status;
	u32		taglen;
	char *		tag;
};

static struct {
int stat;
int errno;
} nfs_cb_errtbl[] = {
	{ NFS4_OK,		0               },
	{ NFS4ERR_PERM,		EPERM           },
	{ NFS4ERR_NOENT,	ENOENT          },
	{ NFS4ERR_IO,		EIO             },
	{ NFS4ERR_NXIO,		ENXIO           },
	{ NFS4ERR_ACCESS,	EACCES          },
	{ NFS4ERR_EXIST,	EEXIST          },
	{ NFS4ERR_XDEV,		EXDEV           },
	{ NFS4ERR_NOTDIR,	ENOTDIR         },
	{ NFS4ERR_ISDIR,	EISDIR          },
	{ NFS4ERR_INVAL,	EINVAL          },
	{ NFS4ERR_FBIG,		EFBIG           },
	{ NFS4ERR_NOSPC,	ENOSPC          },
	{ NFS4ERR_ROFS,		EROFS           },
	{ NFS4ERR_MLINK,	EMLINK          },
	{ NFS4ERR_NAMETOOLONG,	ENAMETOOLONG    },
	{ NFS4ERR_NOTEMPTY,	ENOTEMPTY       },
	{ NFS4ERR_DQUOT,	EDQUOT          },
	{ NFS4ERR_STALE,	ESTALE          },
	{ NFS4ERR_BADHANDLE,	EBADHANDLE      },
	{ NFS4ERR_BAD_COOKIE,	EBADCOOKIE      },
	{ NFS4ERR_NOTSUPP,	ENOTSUPP        },
	{ NFS4ERR_TOOSMALL,	ETOOSMALL       },
	{ NFS4ERR_SERVERFAULT,	ESERVERFAULT    },
	{ NFS4ERR_BADTYPE,	EBADTYPE        },
	{ NFS4ERR_LOCKED,	EAGAIN          },
	{ NFS4ERR_RESOURCE,	EREMOTEIO       },
	{ NFS4ERR_SYMLINK,	ELOOP           },
	{ NFS4ERR_OP_ILLEGAL,	EOPNOTSUPP      },
	{ NFS4ERR_DEADLOCK,	EDEADLK         },
	{ -1,                   EIO             }
};

static int
nfs_cb_stat_to_errno(int stat)
{
	int i;
	for (i = 0; nfs_cb_errtbl[i].stat != -1; i++) {
		if (nfs_cb_errtbl[i].stat == stat)
			return nfs_cb_errtbl[i].errno;
	}
	/* If we cannot translate the error, the recovery routines should
	* handle it.
	* Note: remaining NFSv4 error codes have values > 10000, so should
	* not conflict with native Linux error codes.
	*/
	return stat;
}

/*
 * XDR encode
 */

static int
encode_cb_compound_hdr(struct xdr_stream *xdr, struct nfs4_cb_compound_hdr *hdr)
{
	__be32 * p;

	RESERVE_SPACE(16);
	WRITE32(0);            /* tag length is always 0 */
	WRITE32(0);		/* minorversion */
	WRITE32(hdr->ident);
	WRITE32(hdr->nops);
	return 0;
}

static int
encode_cb_recall(struct xdr_stream *xdr, struct nfs4_cb_recall *cb_rec)
{
	__be32 *p;
	int len = cb_rec->cbr_fhlen;

	RESERVE_SPACE(12+sizeof(cb_rec->cbr_stateid) + len);
	WRITE32(OP_CB_RECALL);
	WRITEMEM(&cb_rec->cbr_stateid, sizeof(stateid_t));
	WRITE32(cb_rec->cbr_trunc);
	WRITE32(len);
	WRITEMEM(cb_rec->cbr_fhval, len);
	return 0;
}

#if defined(CONFIG_NFSD_V4_1)
static int
encode_cb_sequence(struct xdr_stream *xdr, struct nfs41_cb_sequence *args)
{
	u32 *p;

	RESERVE_SPACE(1 + NFS4_MAX_SESSIONID_LEN + 20);

	WRITE32(OP_CB_SEQUENCE);
	WRITEMEM(args->cbs_sessionid, NFS4_MAX_SESSIONID_LEN);
	WRITE32(args->cbs_seqid);
	WRITE32(args->cbs_slotid);
	WRITE32(args->cbs_highest_slotid);
	WRITE32(args->cbsa_cachethis);
	WRITE32(0); /* FIXME: support referring_call_lists */
	return 0;
}

#if defined(CONFIG_PNFSD)
static int
encode_cb_layout(struct xdr_stream *xdr, struct nfs4_layoutrecall *clr)
{
	u32 *p;

	RESERVE_SPACE(20);
	WRITE32(OP_CB_LAYOUT);
	WRITE32(clr->cb.cbl_seg.layout_type);
	WRITE32(clr->cb.cbl_seg.iomode);
	WRITE32(clr->cb.cbl_layoutchanged);
	WRITE32(clr->cb.cbl_recall_type);
	if (unlikely(clr->cb.cbl_recall_type == RECALL_FSID)) {
		struct nfs4_fsid fsid = clr->cb.cbl_fsid;

		RESERVE_SPACE(16);
		WRITE64(fsid.major);
		WRITE64(fsid.minor);
		dprintk("%s: type %x iomode %d changed %d recall_type %d "
			"fsid 0x%llx-0x%llx\n",
			__func__, clr->cb.cbl_seg.layout_type,
			clr->cb.cbl_seg.iomode, clr->cb.cbl_layoutchanged,
			clr->cb.cbl_recall_type, fsid.major, fsid.minor);
	} else if (clr->cb.cbl_recall_type == RECALL_FILE) {
		int len = clr->clr_file->fi_fhlen;

		RESERVE_SPACE(20 + sizeof(stateid_t) + len);
		WRITE32(len);
		WRITEMEM(clr->clr_file->fi_fhval, len);
		WRITE64(clr->cb.cbl_seg.offset);
		WRITE64(clr->cb.cbl_seg.length);
		WRITEMEM(&clr->cb.cbl_sid, sizeof(stateid_t));
		dprintk("%s: type %x iomode %d changed %d recall_type %d "
			"offset %lld length %lld\n",
			__func__, clr->cb.cbl_seg.layout_type,
			clr->cb.cbl_seg.iomode, clr->cb.cbl_layoutchanged,
			clr->cb.cbl_recall_type,
			clr->cb.cbl_seg.offset, clr->cb.cbl_seg.length);
	} else
		dprintk("%s: type %x iomode %d changed %d recall_type %d\n",
			__func__, clr->cb.cbl_seg.layout_type,
			clr->cb.cbl_seg.iomode, clr->cb.cbl_layoutchanged,
			clr->cb.cbl_recall_type);
	return 0;
}

static int
encode_cb_device(struct xdr_stream *xdr, struct nfs4_notify_device *nd)
{
	u32 *p;

	RESERVE_SPACE(28);
	WRITE32(OP_CB_DEVICE);
	WRITE32(nd->cbd.cbd_notify_type);
	WRITE32(nd->cbd.cbd_layout_type);
	WRITE64(nd->cbd.cbd_devid.pnfs_fsid);
	WRITE64(nd->cbd.cbd_devid.pnfs_devid);

	if (nd->cbd.cbd_notify_type == NOTIFY_DEVICEID4_CHANGE) {
		RESERVE_SPACE(4);
		WRITE32(nd->cbd.cbd_immediate);
	}
	dprintk("%s: notify_type %d layout_type 0x%x devid x%llx-x%llx\n",
			__func__, nd->cbd.cbd_notify_type,
			nd->cbd.cbd_layout_type,
			nd->cbd.cbd_devid.pnfs_fsid,
			nd->cbd.cbd_devid.pnfs_devid);
	return 0;
}
#endif /* CONFIG_PNFSD */
#endif /* defined(CONFIG_NFSD_V4_1) */

static int
nfs4_xdr_enc_cb_null(struct rpc_rqst *req, __be32 *p)
{
	struct xdr_stream xdrs, *xdr = &xdrs;

	xdr_init_encode(&xdrs, &req->rq_snd_buf, p);
        RESERVE_SPACE(0);
	return 0;
}

static int
nfs4_xdr_enc_cb_recall(struct rpc_rqst *req, __be32 *p, struct nfs4_cb_recall *args)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = args->cbr_ident,
		.nops   = 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound_hdr(&xdr, &hdr);
	return (encode_cb_recall(&xdr, args));
}

#if defined(CONFIG_NFSD_V4_1)
static int
encode_cb_compound41_hdr(struct xdr_stream *xdr,
			 struct nfs4_cb_compound_hdr *hdr)
{
	u32 *p;

	RESERVE_SPACE(16);
	WRITE32(0);		/* tag length is always 0 */
	WRITE32(1);		/* minorversion */
	WRITE32(0);		/* callback_ident not used in 4.1 */
	WRITE32(hdr->nops);
	return 0;
}

static int
nfs41_xdr_enc_cb_recall(struct rpc_rqst *req, u32 *p,
			struct nfs41_rpc_args *rpc_args)
{
	struct xdr_stream xdr;
	struct nfs4_cb_recall *args = rpc_args->args_op;
	struct nfs4_cb_compound_hdr hdr = {
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound41_hdr(&xdr, &hdr);
	encode_cb_sequence(&xdr, rpc_args->args_seq);
	return encode_cb_recall(&xdr, args);
}

#if defined(CONFIG_PNFSD)
static int
nfs41_xdr_enc_cb_layout(struct rpc_rqst *req, u32 *p,
			struct nfs41_rpc_args *rpc_args)
{
	struct xdr_stream xdr;
	struct nfs4_layoutrecall *args = rpc_args->args_op;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = 0,
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound41_hdr(&xdr, &hdr);
	encode_cb_sequence(&xdr, rpc_args->args_seq);
	return (encode_cb_layout(&xdr, args));
}

static int
nfs41_xdr_enc_cb_device(struct rpc_rqst *req, u32 *p,
			struct nfs41_rpc_args *rpc_args)
{
	struct xdr_stream xdr;
	struct nfs4_notify_device *args = rpc_args->args_op;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = 0,
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound41_hdr(&xdr, &hdr);
	encode_cb_sequence(&xdr, rpc_args->args_seq);
	return (encode_cb_device(&xdr, args));
}
#endif /* CONFIG_PNFSD */
#endif /* defined(CONFIG_NFSD_V4_1) */

static int
decode_cb_compound_hdr(struct xdr_stream *xdr, struct nfs4_cb_compound_hdr *hdr){
        __be32 *p;

        READ_BUF(8);
        READ32(hdr->status);
        READ32(hdr->taglen);
        READ_BUF(hdr->taglen + 4);
        hdr->tag = (char *)p;
        p += XDR_QUADLEN(hdr->taglen);
        READ32(hdr->nops);
        return 0;
}

static int
decode_cb_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	__be32 *p;
	u32 op;
	int32_t nfserr;

	READ_BUF(8);
	READ32(op);
	if (op != expected) {
		dprintk("NFSD: decode_cb_op_hdr: Callback server returned "
		         " operation %d but we issued a request for %d\n",
		         op, expected);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr != NFS_OK)
		return -nfs_cb_stat_to_errno(nfserr);
	return 0;
}

static int
nfs4_xdr_dec_cb_null(struct rpc_rqst *req, __be32 *p)
{
	return 0;
}

static int
nfs4_xdr_dec_cb_recall(struct rpc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_cb_op_hdr(&xdr, OP_CB_RECALL);
out:
	return status;
}

#if defined(CONFIG_NFSD_V4_1)
static int
decode_cb_sequence(struct xdr_stream *xdr, struct nfs41_cb_sequence *res)
{
	int status;
	u32 *p;

	status = decode_cb_op_hdr(xdr, OP_CB_SEQUENCE);
	if (status)
		return status;
	READ_BUF(NFS4_MAX_SESSIONID_LEN + 16);
	COPYMEM(res->cbs_sessionid, NFS4_MAX_SESSIONID_LEN);
	READ32(res->cbs_seqid);
	READ32(res->cbs_slotid);
	READ32(res->cbs_highest_slotid);
	READ32(res->cbsr_target_highest_slotid);
	return 0;
}

static int
nfs41_xdr_dec_cb_recall(struct rpc_rqst *rqstp, u32 *p,
			struct nfs41_rpc_res *rpc_res)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_cb_sequence(&xdr, rpc_res->res_seq);
	if (status)
		goto out;
	status = decode_cb_op_hdr(&xdr, OP_CB_RECALL);
out:
	return status;
}

#if defined(CONFIG_PNFSD)
static int
nfs41_xdr_dec_cb_layout(struct rpc_rqst *rqstp, u32 *p,
			struct nfs41_rpc_res *rpc_res)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_cb_sequence(&xdr, rpc_res->res_seq);
	if (status)
		goto out;
	status = decode_cb_op_hdr(&xdr, OP_CB_LAYOUT);
out:
	return status;
}

static int
nfs41_xdr_dec_cb_device(struct rpc_rqst *rqstp, u32 *p,
			struct nfs41_rpc_res *rpc_res)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_cb_sequence(&xdr, rpc_res->res_seq);
	if (status)
		goto out;
	status = decode_cb_op_hdr(&xdr, OP_CB_DEVICE);
out:
	return status;
}
#endif /* CONFIG_PNFSD */
#endif /* defined(CONFIG_NFSD_V4_1) */

/*
 * RPC procedure tables
 */
#define PROC(proc, call, argtype, restype)                              \
[NFSPROC4_CLNT_##proc] = {                                      	\
        .p_proc   = NFSPROC4_CB_##call,					\
        .p_encode = (kxdrproc_t) nfs4_xdr_##argtype,                    \
        .p_decode = (kxdrproc_t) nfs4_xdr_##restype,                    \
        .p_arglen = NFS4_##argtype##_sz,                                \
        .p_replen = NFS4_##restype##_sz,                                \
        .p_statidx = NFSPROC4_CB_##call,				\
	.p_name   = #proc,                                              \
}

static struct rpc_procinfo     nfs4_cb_procedures[] = {
    PROC(CB_NULL,      NULL,     enc_cb_null,     dec_cb_null),
    PROC(CB_RECALL,    COMPOUND,   enc_cb_recall,      dec_cb_recall),
};

static struct rpc_version       nfs4_cb_version1 = {
	.number                 = 1,
        .nrprocs                = ARRAY_SIZE(nfs4_cb_procedures),
        .procs                  = nfs4_cb_procedures
};

static struct rpc_version *nfs4_cb_version[] = {
	NULL,
	&nfs4_cb_version1,
};

#if defined(CONFIG_NFSD_V4_1)
#define PROC41(proc, call, argtype, restype)                            \
[NFSPROC4_CLNT_##proc] = {                                              \
	.p_proc   = NFSPROC4_CB_##call,                                 \
	.p_encode = (kxdrproc_t) nfs41_xdr_##argtype,                   \
	.p_decode = (kxdrproc_t) nfs41_xdr_##restype,                   \
	.p_arglen = NFS41_##argtype##_sz,                               \
	.p_replen = NFS41_##restype##_sz,                               \
	.p_statidx = NFSPROC4_CB_##call,                                \
	.p_name   = #proc,                                              \
}

static struct rpc_procinfo     nfs41_cb_procedures[] = {
	PROC(CB_NULL,        NULL,       enc_cb_null,        dec_cb_null),
	PROC41(CB_RECALL,    COMPOUND,   enc_cb_recall,      dec_cb_recall),
#if defined(CONFIG_PNFSD)
	PROC41(CB_LAYOUT,    COMPOUND,   enc_cb_layout,      dec_cb_layout),
	PROC41(CB_DEVICE,    COMPOUND,   enc_cb_device,      dec_cb_device),
#endif
};

static struct rpc_version       nfs41_cb_version1 = {
	.number                 = 1,
	.nrprocs                = ARRAY_SIZE(nfs41_cb_procedures),
	.procs                  = nfs41_cb_procedures
};

static struct rpc_version *nfs41_cb_version[] = {
	NULL,
	&nfs41_cb_version1,
};
#endif /* defined(CONFIG_NFSD_V4_1) */

/* Reference counting, callback cleanup, etc., all look racy as heck.
 * And why is cb_set an atomic? */
static int do_probe_callback(void *data)
{
	struct nfs4_client *clp = data;
	struct sockaddr_in	addr;
	struct nfs4_callback    *cb = &clp->cl_callback;
	struct rpc_timeout	timeparms = {
		.to_initval	= (NFSD_LEASE_TIME/4) * HZ,
		.to_retries	= 5,
		.to_maxval	= (NFSD_LEASE_TIME/2) * HZ,
		.to_exponential	= 1,
	};
	struct rpc_program *	program = &cb->cb_program;
	struct rpc_create_args args = {
		.protocol	= IPPROTO_TCP,
		.address	= (struct sockaddr *)&addr,
		.addrsize	= sizeof(addr),
		.timeout	= &timeparms,
		.program	= program,
		.version	= 1,
		.authflavor	= RPC_AUTH_UNIX, /* XXX: need AUTH_GSS... */
		.flags		= (RPC_CLNT_CREATE_NOPING),
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_NULL],
		.rpc_argp       = clp,
	};
	struct rpc_clnt *client;
	int status;

	/* Initialize address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(cb->cb_port);
	addr.sin_addr.s_addr = htonl(cb->cb_addr);

	/* Initialize rpc_program */
	switch (cb->cb_minorversion) {
	case 0:
		program->name = "nfs4_cb";
		program->nrvers = ARRAY_SIZE(nfs4_cb_version);
		program->version = nfs4_cb_version;
		break;
#if defined(CONFIG_NFSD_V4_1)
	case 1:
		program->name = "nfs41_cb";
		program->nrvers = ARRAY_SIZE(nfs41_cb_version);
		program->version = nfs41_cb_version;
		args.bc_sock = container_of(clp->cl_cb_xprt, struct svc_sock,
					    sk_xprt);
		break;
#endif /* CONFIG_NFSD_V4_1 */
	default:
		BUG();
	}

	program->number = cb->cb_prog;
	program->stats = &cb->cb_stat;
	BUG_ON(program->nrvers <= args.version);
	BUG_ON(!program->version[args.version]);
	BUG_ON(program->version[args.version]->number != args.version);
	dprintk("%s: program %s 0x%x nrvers %u version %u minorversion %u\n",
		__func__, program->name, program->number,
		program->nrvers, args.version, cb->cb_minorversion);

	/* Initialize rpc_stat */
	memset(program->stats, 0, sizeof(cb->cb_stat));
	program->stats->program = program;

	/* Create RPC client */
	client = rpc_create(&args);
	if (IS_ERR(client)) {
		dprintk("NFSD: couldn't create callback client\n");
		status = PTR_ERR(client);
		goto out_err;
	}

	status = rpc_call_sync(client, &msg, RPC_TASK_SOFT);

	if (status)
		goto out_release_client;

	cb->cb_client = client;
	atomic_set(&cb->cb_set, 1);
	put_nfs4_client(clp);
	return 0;
out_release_client:
	dprintk("NFSD: synchronous CB_NULL failed. status=%d\n", status);
	rpc_shutdown_client(client);
out_err:
	put_nfs4_client(clp);
	dprintk("NFSD: warning: no callback path to client %.*s\n",
		(int)clp->cl_name.len, clp->cl_name.data);
	return status;
}

/*
 * Set up the callback client and put a NFSPROC4_CB_NULL on the wire...
 */
void
nfsd4_probe_callback(struct nfs4_client *clp)
{
	struct task_struct *t;

	BUG_ON(atomic_read(&clp->cl_callback.cb_set));

	/* the task holds a reference to the nfs4_client struct */
	atomic_inc(&clp->cl_count);

	t = kthread_run(do_probe_callback, clp, "nfs4_cb_probe");

	if (IS_ERR(t))
		atomic_dec(&clp->cl_count);

	return;
}

#if defined(CONFIG_NFSD_V4_1)
/* FIXME: cb_sequence should support referring call lists, cachethis, and multiple slots */
static int
nfs41_cb_sequence_setup(struct nfs4_client *clp, struct nfs41_cb_sequence *args)
{
	u32 *ptr = (u32 *)clp->cl_sessionid;
	dprintk("%s: %u:%u:%u:%u\n", __func__,
		ptr[0], ptr[1], ptr[2], ptr[3]);

	mutex_lock(&clp->cl_cb_mutex);
	memcpy(args->cbs_sessionid, clp->cl_sessionid, NFS4_MAX_SESSIONID_LEN);
	args->cbs_seqid = ++clp->cl_cb_seq_nr;
	args->cbs_slotid = 0;
	args->cbs_highest_slotid = 0;
	args->cbsa_cachethis = 0;
	return 0;
}

static void
nfs41_cb_sequence_done(struct nfs4_client *clp, struct nfs41_cb_sequence *res)
{
	u32 *ptr = (u32 *)res->cbs_sessionid;
	dprintk("%s: %u:%u:%u:%u\n", __func__,
		ptr[0], ptr[1], ptr[2], ptr[3]);

	/* FIXME: support multiple callback slots */
	mutex_unlock(&clp->cl_cb_mutex);
}
#endif /* CONFIG_NFSD_V4_1 */

static int
_nfsd4_cb_recall(struct nfs4_delegation *dp, struct rpc_message *msg)
{
	struct nfs4_client *clp = dp->dl_client;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct nfs4_cb_recall *cbr = &dp->dl_recall;
	int retries = 1;
	int status = 0;

	msg->rpc_proc = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_RECALL];
	msg->rpc_argp = cbr;

	status = rpc_call_sync(clnt, msg, RPC_TASK_SOFT);
	while (retries--) {
		switch (status) {
		case -EIO:
			/* Network partition? */
			atomic_set(&clp->cl_callback.cb_set, 0);
		case -EBADHANDLE:
		case -NFS4ERR_BAD_STATEID:
			/* Race: client probably got cb_recall
			 * before open reply granting delegation */
			break;
		default:
			goto out;
		}
		ssleep(2);
		status = rpc_call_sync(clnt, msg, RPC_TASK_SOFT);
	}
out:
	return status;
}

#if defined(CONFIG_NFSD_V4_1)
static int
_nfsd41_cb_recall(struct nfs4_delegation *dp, struct rpc_message *msg)
{
	struct nfs4_client *clp = dp->dl_client;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct nfs4_cb_recall *cbr = &dp->dl_recall;
	struct nfs41_cb_sequence seq;
	struct nfs41_rpc_args args = {
		.args_op = cbr,
		.args_seq = &seq
	};
	struct nfs41_rpc_res res = {
		.res_seq = &seq
	};
	int status;

	dprintk("NFSD: _nfs41_cb_recall: dp %p\n", dp);

	nfs41_cb_sequence_setup(clp, &seq);
	msg->rpc_proc = &nfs41_cb_procedures[NFSPROC4_CLNT_CB_RECALL];
	msg->rpc_argp = &args;
	msg->rpc_resp = &res;

	status = rpc_call_sync(clnt, msg, RPC_TASK_SOFT);
	nfs41_cb_sequence_done(clp, &seq);

	/* Network partition? */
	if (status == -EIO)
		atomic_set(&clp->cl_callback.cb_set, 0);

	return status;
}
#endif /* defined(CONFIG_NFSD_V4_1) */

/*
 * called with dp->dl_count inc'ed.
 * nfs4_lock_state() may or may not have been called.
 */
void
nfsd4_cb_recall(struct nfs4_delegation *dp)
{
	struct nfs4_client *clp = dp->dl_client;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct nfs4_cb_recall *cbr = &dp->dl_recall;
	struct rpc_message msg;
	int status = 0;

	dprintk("NFSD: nfs4_cb_recall: dp %p\n", dp);

	cbr->cbr_trunc = 0; /* XXX need to implement truncate optimization */
	cbr->cbr_dp = dp;

	memset(&msg, 0, sizeof(msg));

#if defined(CONFIG_NFSD_V4_1)
	if (clp->cl_callback.cb_minorversion == 1) {
		status = _nfsd41_cb_recall(dp, &msg);
		goto out_put_cred;
	}
#endif /* defined(CONFIG_NFSD_V4_1) */

	status = _nfsd4_cb_recall(dp, &msg);

out_put_cred:
	/*
	 * Success or failure, now we're either waiting for lease expiration
	 * or deleg_return.
	 */
	dprintk("NFSD: nfs4_cb_recall: dp %p dl_flock %p dl_count %d\n",
		dp, dp->dl_flock, atomic_read(&dp->dl_count));
	put_nfs4_client(clp);
	rpc_release_client(clnt);
	nfs4_lock_state();
	nfs4_put_delegation(dp);
	nfs4_unlock_state();
	return;
}

#if defined(CONFIG_PNFSD)
/*
 * called with dp->dl_count inc'ed.
 * nfs4_lock_state() may or may not have been called.
 */
int
nfsd4_cb_layout(struct nfs4_layoutrecall *clr)
{
	struct nfs4_client *clp = clr->clr_client;
	struct rpc_clnt *clnt = NULL;
	struct nfs41_cb_sequence seq;
	struct nfs41_rpc_args args = {
		.args_op = clr,
		.args_seq = &seq
	};
	struct nfs41_rpc_res res = {
		.res_seq = &seq
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs41_cb_procedures[NFSPROC4_CLNT_CB_LAYOUT],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	if (clp)
		clnt = clp->cl_callback.cb_client;

	clr->clr_status = -EIO;
	if ((!atomic_read(&clp->cl_callback.cb_set)) || !clnt)
		goto out;

	nfs41_cb_sequence_setup(clp, &seq);
	clr->clr_status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFT);
	nfs41_cb_sequence_done(clp, &seq);

	if (clr->clr_status == -EIO)
		atomic_set(&clp->cl_callback.cb_set, 0);
out:
	/* Success or failure, now we're either waiting for lease expiration
	   or layout_return. */
	dprintk("NFSD: nfsd4_cb_layout: status %d\n", clr->clr_status);
	return clr->clr_status;
}

/*
 * called with dp->dl_count inc'ed.
 * nfs4_lock_state() may or may not have been called.
 */
int
nfsd4_cb_notify_device(struct nfs4_notify_device *cbnd)
{
	struct nfs4_client *clp = cbnd->cbd_client;
	struct rpc_clnt *clnt = NULL;
	struct nfs41_cb_sequence seq;
	struct nfs41_rpc_args args = {
		.args_op = cbnd,
		.args_seq = &seq
	};
	struct nfs41_rpc_res res = {
		.res_seq = &seq
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs41_cb_procedures[NFSPROC4_CLNT_CB_DEVICE],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	if (clp)
		clnt = clp->cl_callback.cb_client;

	cbnd->cbd_status = -EIO;
	if ((!atomic_read(&clp->cl_callback.cb_set)) || !clnt)
		goto out;

	nfs41_cb_sequence_setup(clp, &seq);
	cbnd->cbd_status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFT);
	nfs41_cb_sequence_done(clp, &seq);

	if (cbnd->cbd_status == -EIO)
		atomic_set(&clp->cl_callback.cb_set, 0);
out:
	dprintk("NFSD %s: status %d\n", __func__, cbnd->cbd_status);
	return cbnd->cbd_status;
}
#endif /* CONFIG_PNFSD */
