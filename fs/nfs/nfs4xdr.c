/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Client-side XDR for NFSv4.
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
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include <linux/pnfs_xdr.h>
#include <linux/nfs4_pnfs.h>
#include "nfs4_fs.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

static int nfs4_stat_to_errno(int);

/* NFSv4 COMPOUND tags are only wanted for debugging purposes */
#ifdef DEBUG
#define NFS4_MAXTAGLEN		20
#else
#define NFS4_MAXTAGLEN		0
#endif

/* lock,open owner id: 
 * we currently use size 2 (u64) out of (NFS4_OPAQUE_LIMIT  >> 2)
 */
#define open_owner_id_maxsz	(1 + 4)
#define lock_owner_id_maxsz	(1 + 4)
#define decode_lockowner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define compound_encode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define compound_decode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define op_encode_hdr_maxsz	(1)
#define op_decode_hdr_maxsz	(2)
#define encode_stateid_maxsz	(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_stateid_maxsz	(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define encode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define decode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define encode_putfh_maxsz	(op_encode_hdr_maxsz + 1 + \
				(NFS4_FHSIZE >> 2))
#define decode_putfh_maxsz	(op_decode_hdr_maxsz)
#define encode_putrootfh_maxsz	(op_encode_hdr_maxsz)
#define decode_putrootfh_maxsz	(op_decode_hdr_maxsz)
#define encode_getfh_maxsz      (op_encode_hdr_maxsz)
#define decode_getfh_maxsz      (op_decode_hdr_maxsz + 1 + \
				((3+NFS4_FHSIZE) >> 2))
#define nfs4_fattr_bitmap_maxsz 3
#define encode_getattr_maxsz    (op_encode_hdr_maxsz + nfs4_fattr_bitmap_maxsz)
#define nfs4_name_maxsz		(1 + ((3 + NFS4_MAXNAMLEN) >> 2))
#define nfs4_path_maxsz		(1 + ((3 + NFS4_MAXPATHLEN) >> 2))
#define nfs4_owner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define nfs4_group_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
/* This is based on getfattr, which uses the most attributes: */
#define nfs4_fattr_value_maxsz	(1 + (1 + 2 + 2 + 4 + 2 + 1 + 1 + 2 + 2 + \
				3 + 3 + 3 + nfs4_owner_maxsz + nfs4_group_maxsz))
#define nfs4_fattr_maxsz	(nfs4_fattr_bitmap_maxsz + \
				nfs4_fattr_value_maxsz)
#define decode_getattr_maxsz    (op_decode_hdr_maxsz + nfs4_fattr_maxsz)
#define encode_attrs_maxsz	(nfs4_fattr_bitmap_maxsz + \
				 1 + 2 + 1 + \
				nfs4_owner_maxsz + \
				nfs4_group_maxsz + \
				4 + 4)
#define encode_savefh_maxsz     (op_encode_hdr_maxsz)
#define decode_savefh_maxsz     (op_decode_hdr_maxsz)
#define encode_restorefh_maxsz  (op_encode_hdr_maxsz)
#define decode_restorefh_maxsz  (op_decode_hdr_maxsz)
#define encode_fsinfo_maxsz	(encode_getattr_maxsz)
#define decode_fsinfo_maxsz	(op_decode_hdr_maxsz + 11)
#define encode_renew_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_renew_maxsz	(op_decode_hdr_maxsz)
#define encode_setclientid_maxsz \
				(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_VERIFIER_SIZE) + \
				XDR_QUADLEN(NFS4_SETCLIENTID_NAMELEN) + \
				1 /* sc_prog */ + \
				XDR_QUADLEN(RPCBIND_MAXNETIDLEN) + \
				XDR_QUADLEN(RPCBIND_MAXUADDRLEN) + \
				1) /* sc_cb_ident */
#define decode_setclientid_maxsz \
				(op_decode_hdr_maxsz + \
				2 + \
				1024) /* large value for CLID_INUSE */
#define encode_setclientid_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				3 + (NFS4_VERIFIER_SIZE >> 2))
#define decode_setclientid_confirm_maxsz \
				(op_decode_hdr_maxsz)
#define encode_lookup_maxsz	(op_encode_hdr_maxsz + nfs4_name_maxsz)
#define decode_lookup_maxsz	(op_decode_hdr_maxsz)
#define encode_share_access_maxsz \
				(2)
#define encode_createmode_maxsz	(1 + encode_attrs_maxsz)
#define encode_opentype_maxsz	(1 + encode_createmode_maxsz)
#define encode_claim_null_maxsz	(1 + nfs4_name_maxsz)
#define encode_open_maxsz	(op_encode_hdr_maxsz + \
				2 + encode_share_access_maxsz + 2 + \
				open_owner_id_maxsz + \
				encode_opentype_maxsz + \
				encode_claim_null_maxsz)
#define decode_ace_maxsz	(3 + nfs4_owner_maxsz)
#define decode_delegation_maxsz	(1 + decode_stateid_maxsz + 1 + \
				decode_ace_maxsz)
#define decode_change_info_maxsz	(5)
#define decode_open_maxsz	(op_decode_hdr_maxsz + \
				decode_stateid_maxsz + \
				decode_change_info_maxsz + 1 + \
				nfs4_fattr_bitmap_maxsz + \
				decode_delegation_maxsz)
#define encode_open_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 1)
#define decode_open_confirm_maxsz \
				(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_open_downgrade_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 1 + \
				 encode_share_access_maxsz)
#define decode_open_downgrade_maxsz \
				(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_close_maxsz	(op_encode_hdr_maxsz + \
				 1 + encode_stateid_maxsz)
#define decode_close_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_setattr_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + \
				 encode_attrs_maxsz)
#define decode_setattr_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz)
#define encode_read_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_read_maxsz	(op_decode_hdr_maxsz + 2)
#define encode_readdir_maxsz	(op_encode_hdr_maxsz + \
				 2 + encode_verifier_maxsz + 5)
#define decode_readdir_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz)
#define encode_readlink_maxsz	(op_encode_hdr_maxsz)
#define decode_readlink_maxsz	(op_decode_hdr_maxsz + 1)
#define encode_write_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 4)
#define decode_write_maxsz	(op_decode_hdr_maxsz + \
				 2 + decode_verifier_maxsz)
#define encode_commit_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_commit_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz)
#define encode_remove_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define encode_rename_maxsz	(op_encode_hdr_maxsz + \
				2 * nfs4_name_maxsz)
#define decode_rename_maxsz	(op_decode_hdr_maxsz + 5 + 5)
#define encode_link_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define decode_link_maxsz	(op_decode_hdr_maxsz + 5)
#define encode_lock_maxsz	(op_encode_hdr_maxsz + \
				 7 + \
				 1 + encode_stateid_maxsz + 8)
#define decode_lock_denied_maxsz \
				(8 + decode_lockowner_maxsz)
#define decode_lock_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_lockt_maxsz	(op_encode_hdr_maxsz + 12)
#define decode_lockt_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_locku_maxsz	(op_encode_hdr_maxsz + 3 + \
				 encode_stateid_maxsz + \
				 4)
#define decode_locku_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_access_maxsz	(op_encode_hdr_maxsz + 1)
#define decode_access_maxsz	(op_decode_hdr_maxsz + 2)
#define encode_symlink_maxsz	(op_encode_hdr_maxsz + \
				1 + nfs4_name_maxsz + \
				1 + \
				nfs4_fattr_maxsz)
#define decode_symlink_maxsz	(op_decode_hdr_maxsz + 8)
#define encode_create_maxsz	(op_encode_hdr_maxsz + \
				1 + 2 + nfs4_name_maxsz + \
				encode_attrs_maxsz)
#define decode_create_maxsz	(op_decode_hdr_maxsz + \
				decode_change_info_maxsz + \
				nfs4_fattr_bitmap_maxsz)
#define encode_statfs_maxsz	(encode_getattr_maxsz)
#define decode_statfs_maxsz	(decode_getattr_maxsz)
#define encode_delegreturn_maxsz (op_encode_hdr_maxsz + 4)
#define decode_delegreturn_maxsz (op_decode_hdr_maxsz)
#define encode_getacl_maxsz	(encode_getattr_maxsz)
#define decode_getacl_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz + 1)
#define encode_setacl_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_setacl_maxsz	(decode_setattr_maxsz)
#define encode_fs_locations_maxsz \
				(encode_getattr_maxsz)
#define decode_fs_locations_maxsz \
				(0)

#if defined(CONFIG_NFS_V4_1)
#define encode_exchange_id_maxsz (op_encode_hdr_maxsz + \
				4 /*server->ip_addr*/ + \
				1 /*netid*/ + \
				3 /*cred name*/ + \
				1 /*id_uniquifier*/ + \
				(NFS4_VERIFIER_SIZE >> 2) + \
				1 /*flags*/ + \
				1 /*zero implemetation id array*/)
#define decode_exchange_id_maxsz (op_decode_hdr_maxsz + \
				2 + 1 + 1 + 2 + 1 + \
				(NFS4_OPAQUE_LIMIT >> 2) + 1 + \
				(NFS4_OPAQUE_LIMIT >> 2) + 1)
#define encode_create_session_maxsz	(op_encode_hdr_maxsz + 2 + 2 + \
					 7 + 7 + 4 + 4 + 16)
#define decode_create_session_maxsz	(op_decode_hdr_maxsz +	\
					 2 + 6 + 2 + 6 + 2 +	\
					 XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN))
#define encode_destroy_session_maxsz    (op_encode_hdr_maxsz + 4)
#define decode_destroy_session_maxsz    (op_decode_hdr_maxsz)
#define encode_sequence_maxsz	(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 4)
#define decode_sequence_maxsz	(op_decode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 5)
#endif /* CONFIG_NFS_V4_1 */
#if defined(CONFIG_PNFS)
#define encode_getdevicelist_maxsz (op_encode_hdr_maxsz + 4 + \
				    encode_verifier_maxsz)
#define decode_getdevicelist_maxsz (op_decode_hdr_maxsz + 2 + 1 + 1 +	\
				    decode_verifier_maxsz +		\
				    XDR_QUADLEN(NFS4_PNFS_DEV_MAXNUM *	\
						NFS4_PNFS_DEVICEID4_SIZE))
#define encode_getdeviceinfo_maxsz (op_encode_hdr_maxsz + 4 + \
				    XDR_QUADLEN(NFS4_PNFS_DEVICEID4_SIZE))
#define decode_getdeviceinfo_maxsz (op_decode_hdr_maxsz + 4 + \
				    XDR_QUADLEN(NFS4_PNFS_DEV_MAXSIZE))
#define encode_pnfs_layoutget_sz (op_encode_hdr_maxsz + 10 + \
				  encode_stateid_maxsz)
#define decode_pnfs_layoutget_maxsz	(op_decode_hdr_maxsz + 8 + \
					 decode_stateid_maxsz + \
					 XDR_QUADLEN(PNFS_LAYOUT_MAXSIZE))
#define encode_pnfs_layoutcommit_sz	(18 +				\
					 XDR_QUADLEN(PNFS_LAYOUT_MAXSIZE) + \
					 op_encode_hdr_maxsz +		\
					 encode_stateid_maxsz)
#define decode_pnfs_layoutcommit_maxsz	(3 + op_decode_hdr_maxsz)
#define encode_pnfs_layoutreturn_sz	(8 + op_encode_hdr_maxsz + \
					 encode_stateid_maxsz + \
					 1 /* FIXME: opaque lrf_body always empty at the moment */)
#define decode_pnfs_layoutreturn_maxsz	(op_decode_hdr_maxsz + \
					 1 + decode_stateid_maxsz)
#endif /* CONFIG_PNFS */

#define NFS40_enc_compound_sz	(1024)  /* XXX: large enough? */
#define NFS40_dec_compound_sz	(1024)  /* XXX: large enough? */
#define NFS40_enc_read_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_read_maxsz)
#define NFS40_dec_read_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_read_maxsz)
#define NFS40_enc_readlink_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_readlink_maxsz)
#define NFS40_dec_readlink_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_readlink_maxsz)
#define NFS40_enc_readdir_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_readdir_maxsz)
#define NFS40_dec_readdir_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_readdir_maxsz)
#define NFS40_enc_write_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_write_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_write_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_write_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_commit_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_commit_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_commit_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_commit_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_open_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_open_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_open_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_open_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_open_confirm_sz \
				(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_open_confirm_maxsz)
#define NFS40_dec_open_confirm_sz \
				(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_open_confirm_maxsz)
#define NFS40_enc_open_noattr_sz	(compound_encode_hdr_maxsz + \
					encode_putfh_maxsz + \
					encode_open_maxsz + \
					encode_getattr_maxsz)
#define NFS40_dec_open_noattr_sz	(compound_decode_hdr_maxsz + \
					decode_putfh_maxsz + \
					decode_open_maxsz + \
					decode_getattr_maxsz)
#define NFS40_enc_open_downgrade_sz \
				(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_open_downgrade_maxsz + \
				 encode_getattr_maxsz)
#define NFS40_dec_open_downgrade_sz \
				(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_open_downgrade_maxsz + \
				 decode_getattr_maxsz)
#define NFS40_enc_close_sz	(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_close_maxsz + \
				 encode_getattr_maxsz)
#define NFS40_dec_close_sz	(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_close_maxsz + \
				 decode_getattr_maxsz)
#define NFS40_enc_setattr_sz	(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_setattr_maxsz + \
				 encode_getattr_maxsz)
#define NFS40_dec_setattr_sz	(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_setattr_maxsz + \
				 decode_getattr_maxsz)
#define NFS40_enc_fsinfo_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS40_dec_fsinfo_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_fsinfo_maxsz)
#define NFS40_enc_renew_sz	(compound_encode_hdr_maxsz + \
				encode_renew_maxsz)
#define NFS40_dec_renew_sz	(compound_decode_hdr_maxsz + \
				decode_renew_maxsz)
#define NFS40_enc_setclientid_sz	(compound_encode_hdr_maxsz + \
				encode_setclientid_maxsz)
#define NFS40_dec_setclientid_sz	(compound_decode_hdr_maxsz + \
				decode_setclientid_maxsz)
#define NFS40_enc_setclientid_confirm_sz \
				(compound_encode_hdr_maxsz + \
				encode_setclientid_confirm_maxsz + \
				encode_putrootfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS40_dec_setclientid_confirm_sz \
				(compound_decode_hdr_maxsz + \
				decode_setclientid_confirm_maxsz + \
				decode_putrootfh_maxsz + \
				decode_fsinfo_maxsz)
#define NFS40_enc_lock_sz        (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_lock_maxsz)
#define NFS40_dec_lock_sz        (compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_lock_maxsz)
#define NFS40_enc_lockt_sz       (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_lockt_maxsz)
#define NFS40_dec_lockt_sz       (compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lockt_maxsz)
#define NFS40_enc_locku_sz       (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_locku_maxsz)
#define NFS40_dec_locku_sz       (compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_locku_maxsz)
#define NFS40_enc_access_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_access_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_access_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_access_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_getattr_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_getattr_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_lookup_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_lookup_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS40_dec_lookup_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_lookup_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS40_enc_lookup_root_sz (compound_encode_hdr_maxsz + \
				encode_putrootfh_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS40_dec_lookup_root_sz (compound_decode_hdr_maxsz + \
				decode_putrootfh_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS40_enc_remove_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_remove_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_remove_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 5 + \
				decode_getattr_maxsz)
#define NFS40_enc_rename_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_rename_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_rename_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_rename_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_link_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_link_maxsz + \
				decode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_dec_link_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_link_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_symlink_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_symlink_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS40_dec_symlink_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_symlink_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS40_enc_create_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_create_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_create_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_create_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_pathconf_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_pathconf_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_statfs_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_statfs_maxsz)
#define NFS40_dec_statfs_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_statfs_maxsz)
#define NFS40_enc_server_caps_sz (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_server_caps_sz (compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_delegreturn_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_delegreturn_maxsz + \
				encode_getattr_maxsz)
#define NFS40_dec_delegreturn_sz (compound_decode_hdr_maxsz + \
				decode_delegreturn_maxsz + \
				decode_getattr_maxsz)
#define NFS40_enc_getacl_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getacl_maxsz)
#define NFS40_dec_getacl_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getacl_maxsz)
#define NFS40_enc_setacl_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_setacl_maxsz)
#define NFS40_dec_setacl_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_setacl_maxsz)
#define NFS40_enc_fs_locations_sz \
				(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_lookup_maxsz + \
				 encode_fs_locations_maxsz)
#define NFS40_dec_fs_locations_sz \
				(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lookup_maxsz + \
				 decode_fs_locations_maxsz)
#if defined(CONFIG_NFS_V4_1)
#define NFS41_enc_access_sz	(NFS40_enc_access_sz + \
				 encode_sequence_maxsz)
#define NFS41_dec_access_sz	(NFS40_dec_access_sz + \
				 decode_sequence_maxsz)
#define NFS41_enc_lookup_sz	(NFS40_enc_lookup_sz + \
				 encode_sequence_maxsz)
#define NFS41_dec_lookup_sz	(NFS40_dec_lookup_sz + \
				 decode_sequence_maxsz)
#define NFS41_enc_lookup_root_sz	(NFS40_enc_lookup_root_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_lookup_root_sz	(NFS40_dec_lookup_root_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_remove_sz		(NFS40_enc_remove_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_remove_sz		(NFS40_dec_remove_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_rename_sz		(NFS40_enc_rename_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_rename_sz		(NFS40_dec_rename_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_link_sz		(NFS40_enc_link_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_link_sz		(NFS40_dec_link_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_create_sz		(NFS40_enc_create_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_create_sz		(NFS40_dec_create_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_symlink_sz		(NFS40_enc_symlink_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_symlink_sz		(NFS40_dec_symlink_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_getattr_sz		(NFS40_enc_getattr_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_getattr_sz		(NFS40_dec_getattr_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_close_sz		(NFS40_enc_close_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_close_sz		(NFS40_dec_close_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_open_sz		(NFS40_enc_open_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_open_sz		(NFS40_dec_open_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_open_noattr_sz	(NFS40_enc_open_noattr_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_open_noattr_sz	(NFS40_dec_open_noattr_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_open_downgrade_sz	(NFS40_enc_open_downgrade_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_open_downgrade_sz	(NFS40_dec_open_downgrade_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_lock_sz		(NFS40_enc_lock_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_lock_sz		(NFS40_dec_lock_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_locku_sz		(NFS40_enc_locku_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_locku_sz		(NFS40_dec_locku_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_lockt_sz		(NFS40_enc_lockt_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_lockt_sz		(NFS40_dec_lockt_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_readlink_sz		(NFS40_enc_readlink_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_readlink_sz		(NFS40_dec_readlink_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_readdir_sz		(NFS40_enc_readdir_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_readdir_sz		(NFS40_dec_readdir_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_read_sz		(NFS40_enc_read_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_read_sz		(NFS40_dec_read_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_setattr_sz		(NFS40_enc_setattr_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_setattr_sz		(NFS40_dec_setattr_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_write_sz		(NFS40_enc_write_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_write_sz		(NFS40_dec_write_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_commit_sz		(NFS40_enc_commit_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_commit_sz		(NFS40_dec_commit_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_delegreturn_sz	(NFS40_enc_delegreturn_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_delegreturn_sz	(NFS40_dec_delegreturn_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_fsinfo_sz		(NFS40_enc_fsinfo_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_fsinfo_sz		(NFS40_dec_fsinfo_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_pathconf_sz		(NFS40_enc_pathconf_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_pathconf_sz		(NFS40_dec_pathconf_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_statfs_sz		(NFS40_enc_statfs_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_statfs_sz		(NFS40_dec_statfs_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_server_caps_sz	(NFS40_enc_server_caps_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_server_caps_sz	(NFS40_dec_server_caps_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_getacl_sz		(NFS40_enc_getacl_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_getacl_sz		(NFS40_dec_getacl_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_setacl_sz		(NFS40_enc_setacl_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_setacl_sz		(NFS40_dec_setacl_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_fs_locations_sz	(NFS40_enc_fs_locations_sz + \
					 encode_sequence_maxsz)
#define NFS41_dec_fs_locations_sz	(NFS40_dec_fs_locations_sz + \
					 decode_sequence_maxsz)
#define NFS41_enc_exchange_id_sz \
				(compound_encode_hdr_maxsz + \
				 encode_exchange_id_maxsz)
#define NFS41_dec_exchange_id_sz \
				(compound_decode_hdr_maxsz + \
				 decode_exchange_id_maxsz)
#define NFS41_enc_create_session_sz \
				(compound_encode_hdr_maxsz + \
				 encode_create_session_maxsz)
#define NFS41_dec_create_session_sz \
				(compound_decode_hdr_maxsz + \
				 decode_create_session_maxsz)
#define NFS41_enc_destroy_session_sz	(compound_encode_hdr_maxsz + \
					 encode_destroy_session_maxsz)
#define NFS41_dec_destroy_session_sz	(compound_decode_hdr_maxsz + \
					 decode_destroy_session_maxsz)
#define NFS41_enc_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 encode_sequence_maxsz)
#define NFS41_dec_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz)
#define NFS41_enc_get_lease_time_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putrootfh_maxsz + \
					 encode_fsinfo_maxsz)
#define NFS41_dec_get_lease_time_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putrootfh_maxsz + \
					 decode_fsinfo_maxsz)
#define NFS41_enc_error_sz		(0)
#define NFS41_dec_error_sz		(0)
#endif /* CONFIG_NFS_V4_1 */
#if defined(CONFIG_PNFS)
#define NFS41_enc_pnfs_getdevicelist_sz (compound_encode_hdr_maxsz + \
					encode_sequence_maxsz + \
					encode_putfh_maxsz + \
					encode_getdevicelist_maxsz)
#define NFS41_dec_pnfs_getdevicelist_sz (compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_getdevicelist_maxsz)
#define NFS41_enc_pnfs_getdeviceinfo_sz	(compound_encode_hdr_maxsz +    \
					encode_sequence_maxsz +\
					encode_getdeviceinfo_maxsz)
#define NFS41_dec_pnfs_getdeviceinfo_sz	(compound_decode_hdr_maxsz +    \
					decode_sequence_maxsz + \
					decode_getdeviceinfo_maxsz)
#define NFS41_enc_pnfs_layoutget_sz (compound_encode_hdr_maxsz + \
				     encode_sequence_maxsz + \
				     encode_putfh_maxsz +        \
				     encode_pnfs_layoutget_sz)
#define NFS41_dec_pnfs_layoutget_sz (compound_decode_hdr_maxsz + \
				     decode_sequence_maxsz + \
				     decode_putfh_maxsz +        \
				     decode_pnfs_layoutget_maxsz)
#define NFS41_enc_pnfs_layoutcommit_sz	(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz +\
					encode_putfh_maxsz + \
					encode_pnfs_layoutcommit_sz + \
					encode_getattr_maxsz)
#define NFS41_dec_pnfs_layoutcommit_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_pnfs_layoutcommit_maxsz + \
					decode_getattr_maxsz)
#define NFS41_enc_pnfs_layoutreturn_sz	(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz + \
					encode_putfh_maxsz + \
					encode_pnfs_layoutreturn_sz)
#define NFS41_dec_pnfs_layoutreturn_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_pnfs_layoutreturn_maxsz)
#define NFS41_enc_pnfs_write_sz		(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz +\
					encode_putfh_maxsz + \
					encode_write_maxsz)
#define NFS41_dec_pnfs_write_sz 	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_write_maxsz)
#endif /* CONFIG_PNFS */

static struct {
	unsigned int	mode;
	unsigned int	nfs2type;
} nfs_type2fmt[] = {
	{ 0,		NFNON	     },
	{ S_IFREG,	NFREG	     },
	{ S_IFDIR,	NFDIR	     },
	{ S_IFBLK,	NFBLK	     },
	{ S_IFCHR,	NFCHR	     },
	{ S_IFLNK,	NFLNK	     },
	{ S_IFSOCK,	NFSOCK	     },
	{ S_IFIFO,	NFFIFO	     },
	{ 0,		NFNON	     },
	{ 0,		NFNON	     },
};

struct compound_hdr {
	int32_t		status;
	uint32_t	nops;
	uint32_t	taglen;
	char *		tag;
};

/*
 * START OF "GENERIC" ENCODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((uint32_t)((n) >> 32));				\
	*p++ = htonl((uint32_t)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	p = xdr_encode_opaque_fixed(p, ptr, nbytes);		\
} while (0)

#define RESERVE_SPACE(nbytes)	do {				\
	p = xdr_reserve_space(xdr, nbytes);			\
	BUG_ON(!p);						\
} while (0)

static void encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + len);
	BUG_ON(p == NULL);
	xdr_encode_opaque(p, str, len);
}

static int encode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr
*hdr, int minorversion)
{
	__be32 *p;

	dprintk("encode_compound: tag=%.*s\n", (int)hdr->taglen, hdr->tag);
	BUG_ON(hdr->taglen > NFS4_MAXTAGLEN);
	RESERVE_SPACE(12+(XDR_QUADLEN(hdr->taglen)<<2));
	WRITE32(hdr->taglen);
	WRITEMEM(hdr->tag, hdr->taglen);
	WRITE32(minorversion);
	WRITE32(hdr->nops);
	return 0;
}

static void encode_nfs4_verifier(struct xdr_stream *xdr, const nfs4_verifier *verf)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_VERIFIER_SIZE);
	BUG_ON(p == NULL);
	xdr_encode_opaque_fixed(p, verf->data, NFS4_VERIFIER_SIZE);
}

static int encode_attrs(struct xdr_stream *xdr, const struct iattr *iap, const struct nfs_server *server)
{
	char owner_name[IDMAP_NAMESZ];
	char owner_group[IDMAP_NAMESZ];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	__be32 *p;
	__be32 *q;
	int len;
	uint32_t bmval0 = 0;
	uint32_t bmval1 = 0;
	int status;

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 * In the worst-case, this would be
	 *   12(bitmap) + 4(attrlen) + 8(size) + 4(mode) + 4(atime) + 4(mtime)
	 *          = 36 bytes, plus any contribution from variable-length fields
	 *            such as owner/group.
	 */
	len = 16;

	/* Sigh */
	if (iap->ia_valid & ATTR_SIZE)
		len += 8;
	if (iap->ia_valid & ATTR_MODE)
		len += 4;
	if (iap->ia_valid & ATTR_UID) {
		owner_namelen = nfs_map_uid_to_name(server->nfs_client, iap->ia_uid, owner_name);
		if (owner_namelen < 0) {
			dprintk("nfs: couldn't resolve uid %d to string\n",
					iap->ia_uid);
			/* XXX */
			strcpy(owner_name, "nobody");
			owner_namelen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_namelen) << 2);
	}
	if (iap->ia_valid & ATTR_GID) {
		owner_grouplen = nfs_map_gid_to_group(server->nfs_client, iap->ia_gid, owner_group);
		if (owner_grouplen < 0) {
			dprintk("nfs: couldn't resolve gid %d to string\n",
					iap->ia_gid);
			strcpy(owner_group, "nobody");
			owner_grouplen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_grouplen) << 2);
	}
	if (iap->ia_valid & ATTR_ATIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_ATIME)
		len += 4;
	if (iap->ia_valid & ATTR_MTIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_MTIME)
		len += 4;
	RESERVE_SPACE(len);

	/*
	 * We write the bitmap length now, but leave the bitmap and the attribute
	 * buffer length to be backfilled at the end of this routine.
	 */
	WRITE32(2);
	q = p;
	p += 3;

	if (iap->ia_valid & ATTR_SIZE) {
		bmval0 |= FATTR4_WORD0_SIZE;
		WRITE64(iap->ia_size);
	}
	if (iap->ia_valid & ATTR_MODE) {
		bmval1 |= FATTR4_WORD1_MODE;
		WRITE32(iap->ia_mode & S_IALLUGO);
	}
	if (iap->ia_valid & ATTR_UID) {
		bmval1 |= FATTR4_WORD1_OWNER;
		WRITE32(owner_namelen);
		WRITEMEM(owner_name, owner_namelen);
	}
	if (iap->ia_valid & ATTR_GID) {
		bmval1 |= FATTR4_WORD1_OWNER_GROUP;
		WRITE32(owner_grouplen);
		WRITEMEM(owner_group, owner_grouplen);
	}
	if (iap->ia_valid & ATTR_ATIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_ATIME) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	if (iap->ia_valid & ATTR_MTIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_MTIME) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	
	/*
	 * Now we backfill the bitmap and the attribute buffer length.
	 */
	if (len != ((char *)p - (char *)q) + 4) {
		printk(KERN_ERR "nfs: Attr length error, %u != %Zu\n",
				len, ((char *)p - (char *)q) + 4);
		BUG();
	}
	len = (char *)p - (char *)q - 12;
	*q++ = htonl(bmval0);
	*q++ = htonl(bmval1);
	*q++ = htonl(len);

	status = 0;
/* out: */
	return status;
}

static int encode_access(struct xdr_stream *xdr, u32 access)
{
	__be32 *p;

	RESERVE_SPACE(8);
	WRITE32(OP_ACCESS);
	WRITE32(access);
	
	return 0;
}

static int encode_close(struct xdr_stream *xdr, const struct nfs_closeargs *arg)
{
	__be32 *p;

	RESERVE_SPACE(8+NFS4_STATEID_SIZE);
	WRITE32(OP_CLOSE);
	WRITE32(arg->seqid->sequence->counter);
	WRITEMEM(arg->stateid->data, NFS4_STATEID_SIZE);
	
	return 0;
}

static int encode_commit(struct xdr_stream *xdr, const struct nfs_writeargs *args)
{
	__be32 *p;
        
        RESERVE_SPACE(16);
        WRITE32(OP_COMMIT);
        WRITE64(args->offset);
        WRITE32(args->count);

        return 0;
}

static int encode_create(struct xdr_stream *xdr, const struct nfs4_create_arg *create)
{
	__be32 *p;
	
	RESERVE_SPACE(8);
	WRITE32(OP_CREATE);
	WRITE32(create->ftype);

	switch (create->ftype) {
	case NF4LNK:
		RESERVE_SPACE(4);
		WRITE32(create->u.symlink.len);
		xdr_write_pages(xdr, create->u.symlink.pages, 0, create->u.symlink.len);
		break;

	case NF4BLK: case NF4CHR:
		RESERVE_SPACE(8);
		WRITE32(create->u.device.specdata1);
		WRITE32(create->u.device.specdata2);
		break;

	default:
		break;
	}

	RESERVE_SPACE(4 + create->name->len);
	WRITE32(create->name->len);
	WRITEMEM(create->name->name, create->name->len);

	return encode_attrs(xdr, create->attrs, create->server);
}

static int encode_getattr_one(struct xdr_stream *xdr, uint32_t bitmap)
{
        __be32 *p;

        RESERVE_SPACE(12);
        WRITE32(OP_GETATTR);
        WRITE32(1);
        WRITE32(bitmap);
        return 0;
}

static int encode_getattr_two(struct xdr_stream *xdr, uint32_t bm0, uint32_t bm1)
{
        __be32 *p;

        RESERVE_SPACE(16);
        WRITE32(OP_GETATTR);
        WRITE32(2);
        WRITE32(bm0);
        WRITE32(bm1);
        return 0;
}

static int encode_getfattr(struct xdr_stream *xdr, const u32* bitmask)
{
	return encode_getattr_two(xdr,
			bitmask[0] & nfs4_fattr_bitmap[0],
			bitmask[1] & nfs4_fattr_bitmap[1]);
}

static int encode_fsinfo(struct xdr_stream *xdr, const u32* bitmask)
{
	return encode_getattr_two(xdr, bitmask[0] & nfs4_fsinfo_bitmap[0],
			bitmask[1] & nfs4_fsinfo_bitmap[1]);
}

#ifdef CONFIG_PNFS
/*
 * Encode request to commit a pNFS layout.  Sent to the MDS
 */
static int encode_pnfs_layoutcommit(struct xdr_stream *xdr,
				    const struct pnfs_layoutcommit_arg *args)
{
	uint32_t *p;

	if (args->new_layout_size > PNFS_LAYOUT_MAXSIZE)
		return -EINVAL;

	dprintk("%s: %llu@%llu lbw: %llu type: %d\n", __func__,
		args->lseg.length, args->lseg.offset, args->lastbytewritten,
		args->layout_type);

	RESERVE_SPACE(40 + NFS4_STATEID_SIZE);
	WRITE32(OP_LAYOUTCOMMIT);
	WRITE64(args->lseg.offset);
	WRITE64(args->lseg.length);
	WRITE32(0);     /* reclaim */
	WRITEMEM(args->stateid.data, NFS4_STATEID_SIZE);
	WRITE32(1);     /* newoffset = TRUE */
	WRITE64(args->lastbytewritten);
	WRITE32(args->time_modify_changed != 0);
	if (args->time_modify_changed) {
		RESERVE_SPACE(12);
		WRITE32(0);
		WRITE32(args->time_modify.tv_sec);
		WRITE32(args->time_modify.tv_nsec);
	}
	RESERVE_SPACE(8 + args->new_layout_size);
	WRITE32(args->layout_type);
	WRITE32(args->new_layout_size);
	if (args->new_layout_size > 0)
		WRITEMEM(args->new_layout, args->new_layout_size);
	return 0;
}
#endif /* CONFIG_PNFS */

static int encode_fs_locations(struct xdr_stream *xdr, const u32* bitmask)
{
	return encode_getattr_two(xdr,
				  bitmask[0] & nfs4_fs_locations_bitmap[0],
				  bitmask[1] & nfs4_fs_locations_bitmap[1]);
}

static int encode_getfh(struct xdr_stream *xdr)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(OP_GETFH);

	return 0;
}

static int encode_link(struct xdr_stream *xdr, const struct qstr *name)
{
	__be32 *p;

	RESERVE_SPACE(8 + name->len);
	WRITE32(OP_LINK);
	WRITE32(name->len);
	WRITEMEM(name->name, name->len);
	
	return 0;
}

static inline int nfs4_lock_type(struct file_lock *fl, int block)
{
	if ((fl->fl_type & (F_RDLCK|F_WRLCK|F_UNLCK)) == F_RDLCK)
		return block ? NFS4_READW_LT : NFS4_READ_LT;
	return block ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
}

static inline uint64_t nfs4_lock_length(struct file_lock *fl)
{
	if (fl->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return fl->fl_end - fl->fl_start + 1;
}

/*
 * opcode,type,reclaim,offset,length,new_lock_owner = 32
 * open_seqid,open_stateid,lock_seqid,lock_owner.clientid, lock_owner.id = 40
 */
static int encode_lock(struct xdr_stream *xdr, const struct nfs_lock_args *args)
{
	__be32 *p;

	RESERVE_SPACE(32);
	WRITE32(OP_LOCK);
	WRITE32(nfs4_lock_type(args->fl, args->block));
	WRITE32(args->reclaim);
	WRITE64(args->fl->fl_start);
	WRITE64(nfs4_lock_length(args->fl));
	WRITE32(args->new_lock_owner);
	if (args->new_lock_owner){
		RESERVE_SPACE(4+NFS4_STATEID_SIZE+32);
		WRITE32(args->open_seqid->sequence->counter);
		WRITEMEM(args->open_stateid->data, NFS4_STATEID_SIZE);
		WRITE32(args->lock_seqid->sequence->counter);
		WRITE64(args->lock_owner.clientid);
		WRITE32(16);
		WRITEMEM("lock id:", 8);
		WRITE64(args->lock_owner.id);
	}
	else {
		RESERVE_SPACE(NFS4_STATEID_SIZE+4);
		WRITEMEM(args->lock_stateid->data, NFS4_STATEID_SIZE);
		WRITE32(args->lock_seqid->sequence->counter);
	}

	return 0;
}

static int encode_lockt(struct xdr_stream *xdr, const struct nfs_lockt_args *args)
{
	__be32 *p;

	RESERVE_SPACE(52);
	WRITE32(OP_LOCKT);
	WRITE32(nfs4_lock_type(args->fl, 0));
	WRITE64(args->fl->fl_start);
	WRITE64(nfs4_lock_length(args->fl));
	WRITE64(args->lock_owner.clientid);
	WRITE32(16);
	WRITEMEM("lock id:", 8);
	WRITE64(args->lock_owner.id);

	return 0;
}

static int encode_locku(struct xdr_stream *xdr, const struct nfs_locku_args *args)
{
	__be32 *p;

	RESERVE_SPACE(12+NFS4_STATEID_SIZE+16);
	WRITE32(OP_LOCKU);
	WRITE32(nfs4_lock_type(args->fl, 0));
	WRITE32(args->seqid->sequence->counter);
	WRITEMEM(args->stateid->data, NFS4_STATEID_SIZE);
	WRITE64(args->fl->fl_start);
	WRITE64(nfs4_lock_length(args->fl));

	return 0;
}

static int encode_lookup(struct xdr_stream *xdr, const struct qstr *name)
{
	int len = name->len;
	__be32 *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_LOOKUP);
	WRITE32(len);
	WRITEMEM(name->name, len);

	return 0;
}

static void encode_share_access(struct xdr_stream *xdr, int open_flags)
{
	__be32 *p;

	RESERVE_SPACE(8);
	switch (open_flags & (FMODE_READ|FMODE_WRITE)) {
		case FMODE_READ:
			WRITE32(NFS4_SHARE_ACCESS_READ);
			break;
		case FMODE_WRITE:
			WRITE32(NFS4_SHARE_ACCESS_WRITE);
			break;
		case FMODE_READ|FMODE_WRITE:
			WRITE32(NFS4_SHARE_ACCESS_BOTH);
			break;
		default:
			BUG();
	}
	WRITE32(0);		/* for linux, share_deny = 0 always */
}

static inline void encode_openhdr(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;
 /*
 * opcode 4, seqid 4, share_access 4, share_deny 4, clientid 8, ownerlen 4,
 * owner 4 = 32
 */
	RESERVE_SPACE(8);
	WRITE32(OP_OPEN);
	WRITE32(arg->seqid->sequence->counter);
	encode_share_access(xdr, arg->open_flags);
	RESERVE_SPACE(28);
	WRITE64(arg->clientid);
	WRITE32(16);
	WRITEMEM("open id:", 8);
	WRITE64(arg->id);
}

static inline void encode_createmode(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;

	RESERVE_SPACE(4);
	switch(arg->open_flags & O_EXCL) {
		case 0:
			WRITE32(NFS4_CREATE_UNCHECKED);
			encode_attrs(xdr, arg->u.attrs, arg->server);
			break;
		default:
			WRITE32(NFS4_CREATE_EXCLUSIVE);
			encode_nfs4_verifier(xdr, &arg->u.verifier);
	}
}

static void encode_opentype(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;

	RESERVE_SPACE(4);
	switch (arg->open_flags & O_CREAT) {
		case 0:
			WRITE32(NFS4_OPEN_NOCREATE);
			break;
		default:
			BUG_ON(arg->claim != NFS4_OPEN_CLAIM_NULL);
			WRITE32(NFS4_OPEN_CREATE);
			encode_createmode(xdr, arg);
	}
}

static inline void encode_delegation_type(struct xdr_stream *xdr, int delegation_type)
{
	__be32 *p;

	RESERVE_SPACE(4);
	switch (delegation_type) {
		case 0:
			WRITE32(NFS4_OPEN_DELEGATE_NONE);
			break;
		case FMODE_READ:
			WRITE32(NFS4_OPEN_DELEGATE_READ);
			break;
		case FMODE_WRITE|FMODE_READ:
			WRITE32(NFS4_OPEN_DELEGATE_WRITE);
			break;
		default:
			BUG();
	}
}

static inline void encode_claim_null(struct xdr_stream *xdr, const struct qstr *name)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(NFS4_OPEN_CLAIM_NULL);
	encode_string(xdr, name->len, name->name);
}

static inline void encode_claim_previous(struct xdr_stream *xdr, int type)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(NFS4_OPEN_CLAIM_PREVIOUS);
	encode_delegation_type(xdr, type);
}

static inline void encode_claim_delegate_cur(struct xdr_stream *xdr, const struct qstr *name, const nfs4_stateid *stateid)
{
	__be32 *p;

	RESERVE_SPACE(4+NFS4_STATEID_SIZE);
	WRITE32(NFS4_OPEN_CLAIM_DELEGATE_CUR);
	WRITEMEM(stateid->data, NFS4_STATEID_SIZE);
	encode_string(xdr, name->len, name->name);
}

static int encode_open(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	encode_openhdr(xdr, arg);
	encode_opentype(xdr, arg);
	switch (arg->claim) {
		case NFS4_OPEN_CLAIM_NULL:
			encode_claim_null(xdr, arg->name);
			break;
		case NFS4_OPEN_CLAIM_PREVIOUS:
			encode_claim_previous(xdr, arg->u.delegation_type);
			break;
		case NFS4_OPEN_CLAIM_DELEGATE_CUR:
			encode_claim_delegate_cur(xdr, arg->name, &arg->u.delegation);
			break;
		default:
			BUG();
	}
	return 0;
}

static int encode_open_confirm(struct xdr_stream *xdr, const struct nfs_open_confirmargs *arg)
{
	__be32 *p;

	RESERVE_SPACE(4+NFS4_STATEID_SIZE+4);
	WRITE32(OP_OPEN_CONFIRM);
	WRITEMEM(arg->stateid->data, NFS4_STATEID_SIZE);
	WRITE32(arg->seqid->sequence->counter);

	return 0;
}

static int encode_open_downgrade(struct xdr_stream *xdr, const struct nfs_closeargs *arg)
{
	__be32 *p;

	RESERVE_SPACE(4+NFS4_STATEID_SIZE+4);
	WRITE32(OP_OPEN_DOWNGRADE);
	WRITEMEM(arg->stateid->data, NFS4_STATEID_SIZE);
	WRITE32(arg->seqid->sequence->counter);
	encode_share_access(xdr, arg->open_flags);
	return 0;
}

static int
encode_putfh(struct xdr_stream *xdr, const struct nfs_fh *fh)
{
	int len = fh->size;
	__be32 *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_PUTFH);
	WRITE32(len);
	WRITEMEM(fh->data, len);

	return 0;
}

static int encode_putrootfh(struct xdr_stream *xdr)
{
        __be32 *p;
        
        RESERVE_SPACE(4);
        WRITE32(OP_PUTROOTFH);

        return 0;
}

static void encode_stateid(struct xdr_stream *xdr, const struct nfs_open_context *ctx)
{
	nfs4_stateid stateid;
	__be32 *p;

	RESERVE_SPACE(NFS4_STATEID_SIZE);
	if (ctx->state != NULL) {
		nfs4_copy_stateid(&stateid, ctx->state, ctx->lockowner);
		WRITEMEM(stateid.data, NFS4_STATEID_SIZE);
	} else
		WRITEMEM(zero_stateid.data, NFS4_STATEID_SIZE);
}

static int encode_read(struct xdr_stream *xdr, const struct nfs_readargs *args)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(OP_READ);

	encode_stateid(xdr, args->context);

	RESERVE_SPACE(12);
	WRITE64(args->offset);
	WRITE32(args->count);

	return 0;
}

static int encode_readdir(struct xdr_stream *xdr, const struct nfs4_readdir_arg *readdir, struct rpc_rqst *req)
{
	uint32_t attrs[2] = {
		FATTR4_WORD0_RDATTR_ERROR|FATTR4_WORD0_FILEID,
		FATTR4_WORD1_MOUNTED_ON_FILEID,
	};
	__be32 *p;

	RESERVE_SPACE(12+NFS4_VERIFIER_SIZE+20);
	WRITE32(OP_READDIR);
	WRITE64(readdir->cookie);
	WRITEMEM(readdir->verifier.data, NFS4_VERIFIER_SIZE);
	WRITE32(readdir->count >> 1);  /* We're not doing readdirplus */
	WRITE32(readdir->count);
	WRITE32(2);
	/* Switch to mounted_on_fileid if the server supports it */
	if (readdir->bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)
		attrs[0] &= ~FATTR4_WORD0_FILEID;
	else
		attrs[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;
	WRITE32(attrs[0] & readdir->bitmask[0]);
	WRITE32(attrs[1] & readdir->bitmask[1]);
	dprintk("%s: cookie = %Lu, verifier = %08x:%08x, bitmap = %08x:%08x\n",
			__func__,
			(unsigned long long)readdir->cookie,
			((u32 *)readdir->verifier.data)[0],
			((u32 *)readdir->verifier.data)[1],
			attrs[0] & readdir->bitmask[0],
			attrs[1] & readdir->bitmask[1]);

	return 0;
}

static int encode_readlink(struct xdr_stream *xdr, const struct nfs4_readlink *readlink, struct rpc_rqst *req)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(OP_READLINK);

	return 0;
}

static int encode_remove(struct xdr_stream *xdr, const struct qstr *name)
{
	__be32 *p;

	RESERVE_SPACE(8 + name->len);
	WRITE32(OP_REMOVE);
	WRITE32(name->len);
	WRITEMEM(name->name, name->len);

	return 0;
}

static int encode_rename(struct xdr_stream *xdr, const struct qstr *oldname, const struct qstr *newname)
{
	__be32 *p;

	RESERVE_SPACE(8 + oldname->len);
	WRITE32(OP_RENAME);
	WRITE32(oldname->len);
	WRITEMEM(oldname->name, oldname->len);
	
	RESERVE_SPACE(4 + newname->len);
	WRITE32(newname->len);
	WRITEMEM(newname->name, newname->len);

	return 0;
}

static int encode_renew(struct xdr_stream *xdr, const struct nfs_client *client_stateid)
{
	__be32 *p;

	RESERVE_SPACE(12);
	WRITE32(OP_RENEW);
	WRITE64(client_stateid->cl_clientid);

	return 0;
}

static int
encode_restorefh(struct xdr_stream *xdr)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(OP_RESTOREFH);

	return 0;
}

static int
encode_setacl(struct xdr_stream *xdr, struct nfs_setaclargs *arg)
{
	__be32 *p;

	RESERVE_SPACE(4+NFS4_STATEID_SIZE);
	WRITE32(OP_SETATTR);
	WRITEMEM(zero_stateid.data, NFS4_STATEID_SIZE);
	RESERVE_SPACE(2*4);
	WRITE32(1);
	WRITE32(FATTR4_WORD0_ACL);
	if (arg->acl_len % 4)
		return -EINVAL;
	RESERVE_SPACE(4);
	WRITE32(arg->acl_len);
	xdr_write_pages(xdr, arg->acl_pages, arg->acl_pgbase, arg->acl_len);
	return 0;
}

static int
encode_savefh(struct xdr_stream *xdr)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(OP_SAVEFH);

	return 0;
}

static int encode_setattr(struct xdr_stream *xdr, const struct nfs_setattrargs *arg, const struct nfs_server *server)
{
	int status;
	__be32 *p;
	
        RESERVE_SPACE(4+NFS4_STATEID_SIZE);
        WRITE32(OP_SETATTR);
	WRITEMEM(arg->stateid.data, NFS4_STATEID_SIZE);

        if ((status = encode_attrs(xdr, arg->iap, server)))
		return status;

        return 0;
}

static int encode_setclientid(struct xdr_stream *xdr, const struct nfs4_setclientid *setclientid)
{
	__be32 *p;

	RESERVE_SPACE(4 + NFS4_VERIFIER_SIZE);
	WRITE32(OP_SETCLIENTID);
	WRITEMEM(setclientid->sc_verifier->data, NFS4_VERIFIER_SIZE);

	encode_string(xdr, setclientid->sc_name_len, setclientid->sc_name);
	RESERVE_SPACE(4);
	WRITE32(setclientid->sc_prog);
	encode_string(xdr, setclientid->sc_netid_len, setclientid->sc_netid);
	encode_string(xdr, setclientid->sc_uaddr_len, setclientid->sc_uaddr);
	RESERVE_SPACE(4);
	WRITE32(setclientid->sc_cb_ident);

	return 0;
}

static int encode_setclientid_confirm(struct xdr_stream *xdr, const struct nfs_client *client_state)
{
        __be32 *p;

        RESERVE_SPACE(12 + NFS4_VERIFIER_SIZE);
        WRITE32(OP_SETCLIENTID_CONFIRM);
        WRITE64(client_state->cl_clientid);
        WRITEMEM(client_state->cl_confirm.data, NFS4_VERIFIER_SIZE);

        return 0;
}

static int encode_write(struct xdr_stream *xdr, const struct nfs_writeargs *args)
{
	__be32 *p;

	RESERVE_SPACE(4);
	WRITE32(OP_WRITE);

	encode_stateid(xdr, args->context);

	RESERVE_SPACE(16);
	WRITE64(args->offset);
	WRITE32(args->stable);
	WRITE32(args->count);

	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);

	return 0;
}

static int encode_delegreturn(struct xdr_stream *xdr, const nfs4_stateid *stateid)
{
	__be32 *p;

	RESERVE_SPACE(4+NFS4_STATEID_SIZE);

	WRITE32(OP_DELEGRETURN);
	WRITEMEM(stateid->data, NFS4_STATEID_SIZE);
	return 0;

}

#if defined(CONFIG_NFS_V4_1)
/* NFSv4.1 operations */
static int encode_exchange_id(struct xdr_stream *xdr,
			      struct nfs41_exchange_id_args *args)
{

	uint32_t *p;

	RESERVE_SPACE(4 + sizeof(args->verifier->data));
	WRITE32(OP_EXCHANGE_ID);
	WRITEMEM(args->verifier->data, sizeof(args->verifier->data));

	encode_string(xdr, args->id_len, args->id);

	RESERVE_SPACE(12);
	WRITE32(args->flags);
	WRITE32(0);	/* zero length state_protect4_a */
	WRITE32(0);     /* zero length implementation id array */

	return 0;
}

static int encode_create_session(struct xdr_stream *xdr,
				 struct nfs41_create_session_args *args)
{
	uint32_t *p;
	char machine_name[64];
	uint32_t len;
	struct nfs_client *clp = args->client;

	RESERVE_SPACE(4);
	WRITE32(OP_CREATE_SESSION);

	RESERVE_SPACE(8);
	WRITE64(clp->cl_clientid);

	RESERVE_SPACE(8);
	WRITE32(clp->cl_seqid);			/*Sequence id */
	WRITE32(args->flags);			/*flags */

	RESERVE_SPACE(2*28);			/* 2 channel_attrs */
	/* Fore Channel */
	WRITE32(args->fc_attrs.headerpadsz);	/* header padding size */
	WRITE32(args->fc_attrs.max_rqst_sz);	/* max req size */
	WRITE32(args->fc_attrs.max_resp_sz);	/* max resp size */
	WRITE32(args->fc_attrs.max_resp_sz_cached);	/* Max resp sz cached */
	WRITE32(args->fc_attrs.max_ops);	/* max operations */
	WRITE32(args->fc_attrs.max_reqs);	/* max requests */
	WRITE32(0);				/* rdmachannel_attrs */

	/* Back Channel */
	WRITE32(args->fc_attrs.headerpadsz);	/* header padding size */
	WRITE32(args->bc_attrs.max_rqst_sz);	/* max req size */
	WRITE32(args->bc_attrs.max_resp_sz);	/* max resp size */
	WRITE32(args->bc_attrs.max_resp_sz_cached);	/* Max resp sz cached */
	WRITE32(args->bc_attrs.max_ops);	/* max operations */
	WRITE32(args->bc_attrs.max_reqs);	/* max requests */
	WRITE32(0);				/* rdmachannel_attrs */

	RESERVE_SPACE(4);
	WRITE32(args->cb_program);		/* cb_program */

	RESERVE_SPACE(4);			/* # of security flavors */
	WRITE32(1);

	RESERVE_SPACE(4);
	WRITE32(RPC_AUTH_UNIX);			/* auth_sys */

	/* authsys_parms rfc1831 */
	RESERVE_SPACE(4);
	WRITE32((u32)clp->cl_boot_time.tv_nsec);	/* stamp */
	len = scnprintf(machine_name, sizeof(machine_name), "%s",
			clp->cl_ipaddr);
	RESERVE_SPACE(16 + len);
	WRITE32(len);
	WRITEMEM(machine_name, len);
	WRITE32(0);				/* UID */
	WRITE32(0);				/* GID */
	WRITE32(0);				/* No more gids */

	return 0;
}

static int encode_destroy_session(struct xdr_stream *xdr,
				  struct nfs4_session *session)
{
	uint32_t *p;
	RESERVE_SPACE(4 + NFS4_MAX_SESSIONID_LEN);
	WRITE32(OP_DESTROY_SESSION);
	WRITEMEM(session->sess_id, NFS4_MAX_SESSIONID_LEN);

	return 0;
}

static int encode_sequence(struct xdr_stream *xdr,
			   const struct nfs41_sequence_args *args)
{
	__be32 *p;

	WARN_ON(args->sa_slotid < 0);

	RESERVE_SPACE(4);
	WRITE32(OP_SEQUENCE);

	/*
	 * Sessionid + seqid + slotid + max slotid + cache_this
	 */
	dprintk("%s: sessionid=%u:%u:%u:%u seqid=%d slotid=%d "
		"max_slotid=%d cache_this=%d\n",
		__func__, ((u32 *)args->sa_sessionid.data)[0],
		((u32 *)args->sa_sessionid.data)[1],
		((u32 *)args->sa_sessionid.data)[2],
		((u32 *)args->sa_sessionid.data)[3],
		args->sa_seqid, args->sa_slotid, args->sa_max_slotid,
		args->sa_cache_this);
	RESERVE_SPACE(NFS4_MAX_SESSIONID_LEN + 16);
	WRITEMEM(args->sa_sessionid.data, NFS4_MAX_SESSIONID_LEN);
	WRITE32(args->sa_seqid);
	WRITE32(args->sa_slotid);
	WRITE32(args->sa_max_slotid);
	WRITE32(args->sa_cache_this);

	return 0;
}
#endif /* CONFIG_NFS_V4_1 */

#if defined(CONFIG_PNFS)
/*
 * Encode request to get information for the list of Data Server devices
 */
static int encode_getdevicelist(struct xdr_stream *xdr,
				const struct nfs4_pnfs_getdevicelist_arg *args)
{
	uint32_t *p;
	nfs4_verifier dummy = {
		.data = "dummmmmy",
	};

	RESERVE_SPACE(20);
	WRITE32(OP_GETDEVICELIST);
	WRITE32(args->layoutclass);
	WRITE32(NFS4_PNFS_DEV_MAXNUM);
	WRITE64(0ULL);				/* cookie */
	encode_nfs4_verifier(xdr, &dummy);

	return 0;
}

/*
 * Encode request to get information for a specific device.
 */
static int encode_getdeviceinfo(struct xdr_stream *xdr,
				const struct nfs4_pnfs_getdeviceinfo_arg *args)
{
	int has_bitmap = (args->dev_notify_types != 0);
	__be32 *p;

	RESERVE_SPACE(16 + NFS4_PNFS_DEVICEID4_SIZE + (has_bitmap * 4));
	WRITE32(OP_GETDEVICEINFO);
	WRITEMEM(args->dev_id->data, NFS4_PNFS_DEVICEID4_SIZE);
	WRITE32(args->layoutclass);
	WRITE32(NFS4_PNFS_DEV_MAXSIZE);
	WRITE32(has_bitmap); 		/* bitmap array length 0 or 1 */
	if (has_bitmap)
		WRITE32(args->dev_notify_types);
	return 0;
}

/*
 * Encode request to get pNFS layout.  Sent to the MDS
 */
static int encode_pnfs_layoutget(struct xdr_stream *xdr,
				 const struct nfs4_pnfs_layoutget_arg *args)
{
	uint32_t *p;

	RESERVE_SPACE(44 + NFS4_STATEID_SIZE);
	WRITE32(OP_LAYOUTGET);
	WRITE32(0);     /* Signal layout available */
	WRITE32(args->type);
	WRITE32(args->lseg.iomode);
	WRITE64(args->lseg.offset);
	WRITE64(args->lseg.length);
	WRITE64(args->minlength);
	WRITEMEM(&args->stateid.data, NFS4_STATEID_SIZE);
	WRITE32(args->maxcount);

	dprintk("%s: 1st type:0x%x iomode:%d off:%lu len:%lu mc:%d\n",
		__func__,
		args->type,
		args->lseg.iomode,
		(unsigned long)args->lseg.offset,
		(unsigned long)args->lseg.length,
		args->maxcount);
	return 0;
}
/*
 * Encode request to return a pNFS layout.  Sent to the MDS
 */
static int encode_pnfs_layoutreturn(struct xdr_stream *xdr,
				const struct nfs4_pnfs_layoutreturn_arg *args)
{
	uint32_t *p;

	RESERVE_SPACE(20);
	WRITE32(OP_LAYOUTRETURN);
	WRITE32(args->reclaim);
	WRITE32(args->layout_type);
	WRITE32(args->lseg.iomode);
	WRITE32(args->return_type);
	if (args->return_type == RETURN_FILE) {
		RESERVE_SPACE(20 + NFS4_STATEID_SIZE);
		WRITE64(args->lseg.offset);
		WRITE64(args->lseg.length);
		WRITEMEM(&args->stateid.data, NFS4_STATEID_SIZE);
		WRITE32(0); /* FIXME: opaque lrf_body always empty at the moment */
	}
	return 0;
}

#endif /* CONFIG_PNFS */
/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

#if defined(CONFIG_NFS_V4_1)
static void nfs41_xdr_enc_error(struct rpc_rqst *req, __be32 *p, void *args)
{
	BUG();
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode an ACCESS request
 */
static int nfs4_xdr_enc_access(struct xdr_stream *xdr, const struct nfs4_accessargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status != 0)
		goto out;
	status = encode_access(xdr, args->access);
	if (status != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_access(struct rpc_rqst *req, __be32 *p, const struct nfs4_accessargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);
	return nfs4_xdr_enc_access(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_access(struct rpc_rqst *req, __be32 *p,
				const struct nfs4_accessargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);
	return nfs4_xdr_enc_access(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode LOOKUP request
 */
static int nfs4_xdr_enc_lookup(struct xdr_stream *xdr, const struct nfs4_lookup_arg *args)
{
	int status;

	if ((status = encode_putfh(xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_lookup(xdr, args->name)) != 0)
		goto out;
	if ((status = encode_getfh(xdr)) != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_lookup(struct rpc_rqst *req, __be32 *p, const struct nfs4_lookup_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_lookup(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_lookup(struct rpc_rqst *req, __be32 *p,
				const struct nfs4_lookup_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 5,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_lookup(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode LOOKUP_ROOT request
 */
static int nfs4_xdr_enc_lookup_root(struct xdr_stream *xdr, const struct nfs4_lookup_root_arg *args)
{
	int status;

	if ((status = encode_putrootfh(xdr)) != 0)
		goto out;
	if ((status = encode_getfh(xdr)) == 0)
		status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_lookup_root(struct rpc_rqst *req, __be32 *p, const struct nfs4_lookup_root_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_lookup_root(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_lookup_root(struct rpc_rqst *req, __be32 *p,
				     const struct nfs4_lookup_root_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_lookup_root(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode REMOVE request
 */
static int nfs4_xdr_enc_remove(struct xdr_stream *xdr, const struct nfs_removeargs *args)
{
	int status;

	if ((status = encode_putfh(xdr, args->fh)) != 0)
		goto out;
	if ((status = encode_remove(xdr, &args->name)) != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_remove(struct rpc_rqst *req, __be32 *p, const struct nfs_removeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_remove(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_remove(struct rpc_rqst *req, __be32 *p,
				const struct nfs_removeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_remove(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode RENAME request
 */
static int nfs4_xdr_enc_rename(struct xdr_stream *xdr, const struct nfs4_rename_arg *args)
{
	int status;

	if ((status = encode_putfh(xdr, args->old_dir)) != 0)
		goto out;
	if ((status = encode_savefh(xdr)) != 0)
		goto out;
	if ((status = encode_putfh(xdr, args->new_dir)) != 0)
		goto out;
	if ((status = encode_rename(xdr, args->old_name, args->new_name)) != 0)
		goto out;
	if ((status = encode_getfattr(xdr, args->bitmask)) != 0)
		goto out;
	if ((status = encode_restorefh(xdr)) != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_rename(struct rpc_rqst *req, __be32 *p, const struct nfs4_rename_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_rename(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_rename(struct rpc_rqst *req, __be32 *p,
				const struct nfs4_rename_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 8,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_rename(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode LINK request
 */
static int nfs4_xdr_enc_link(struct xdr_stream *xdr, const struct nfs4_link_arg *args)
{
	int status;

	if ((status = encode_putfh(xdr, args->fh)) != 0)
		goto out;
	if ((status = encode_savefh(xdr)) != 0)
		goto out;
	if ((status = encode_putfh(xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_link(xdr, args->name)) != 0)
		goto out;
	if ((status = encode_getfattr(xdr, args->bitmask)) != 0)
		goto out;
	if ((status = encode_restorefh(xdr)) != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_link(struct rpc_rqst *req, __be32 *p, const struct nfs4_link_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_link(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_link(struct rpc_rqst *req, __be32 *p,
			      const struct nfs4_link_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 8,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_link(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode CREATE request
 */
static int nfs4_xdr_enc_create(struct xdr_stream *xdr, const struct nfs4_create_arg *args)
{
	int status;

	if ((status = encode_putfh(xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_savefh(xdr)) != 0)
		goto out;
	if ((status = encode_create(xdr, args)) != 0)
		goto out;
	if ((status = encode_getfh(xdr)) != 0)
		goto out;
	if ((status = encode_getfattr(xdr, args->bitmask)) != 0)
		goto out;
	if ((status = encode_restorefh(xdr)) != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_create(struct rpc_rqst *req, __be32 *p, const struct nfs4_create_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_create(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_create(struct rpc_rqst *req, __be32 *p,
				const struct nfs4_create_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 8,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_create(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode SYMLINK request
 */
static int nfs40_xdr_enc_symlink(struct rpc_rqst *req, __be32 *p, const struct nfs4_create_arg *args)
{
	return nfs40_xdr_enc_create(req, p, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_symlink(struct rpc_rqst *req, __be32 *p,
				 const struct nfs4_create_arg *args)
{
	return nfs41_xdr_enc_create(req, p, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode GETATTR request
 */
static int nfs4_xdr_enc_getattr(struct xdr_stream *xdr, const struct nfs4_getattr_arg *args)
{
	int status;

	if ((status = encode_putfh(xdr, args->fh)) == 0)
		status = encode_getfattr(xdr, args->bitmask);
	return status;
}

static int nfs40_xdr_enc_getattr(struct rpc_rqst *req, __be32 *p, const struct nfs4_getattr_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_getattr(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_getattr(struct rpc_rqst *req, __be32 *p,
				 const struct nfs4_getattr_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_getattr(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a CLOSE request
 */
static int nfs4_xdr_enc_close(struct xdr_stream *xdr, struct nfs_closeargs *args)
{
        int status;

        status = encode_putfh(xdr, args->fh);
        if(status)
                goto out;
        status = encode_close(xdr, args);
	if (status != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
        return status;
}

static int nfs40_xdr_enc_close(struct rpc_rqst *req, __be32 *p, struct nfs_closeargs *args)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 3,
        };

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_close(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_close(struct rpc_rqst *req, __be32 *p,
			       struct nfs_closeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_close(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode an OPEN request
 */
static int nfs4_xdr_enc_open(struct xdr_stream *xdr, struct nfs_openargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_savefh(xdr);
	if (status)
		goto out;
	status = encode_open(xdr, args);
	if (status)
		goto out;
	status = encode_getfh(xdr);
	if (status)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
	if (status)
		goto out;
	status = encode_restorefh(xdr);
	if (status)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_open(struct rpc_rqst *req, __be32 *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_open(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_open(struct rpc_rqst *req, __be32 *p,
			      struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 8,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_open(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode an OPEN_CONFIRM request
 */
static int nfs40_xdr_enc_open_confirm(struct rpc_rqst *req, __be32 *p, struct nfs_open_confirmargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_open_confirm(&xdr, args);
out:
	return status;
}

/*
 * Encode an OPEN request with no attributes.
 */
static int nfs4_xdr_enc_open_noattr(struct xdr_stream *xdr, struct nfs_openargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_open(xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_open_noattr(struct rpc_rqst *req, __be32 *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_open_noattr(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_open_noattr(struct rpc_rqst *req, __be32 *p,
				     struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_open_noattr(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode an OPEN_DOWNGRADE request
 */
static int nfs4_xdr_enc_open_downgrade(struct xdr_stream *xdr, struct nfs_closeargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_open_downgrade(xdr, args);
	if (status != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_open_downgrade(struct rpc_rqst *req, __be32 *p, struct nfs_closeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_open_downgrade(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_open_downgrade(struct rpc_rqst *req, __be32 *p,
					struct nfs_closeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_open_downgrade(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a LOCK request
 */
static int nfs4_xdr_enc_lock(struct xdr_stream *xdr, struct nfs_lock_args *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if(status)
		goto out;
	status = encode_lock(xdr, args);
out:
	return status;
}

static int nfs40_xdr_enc_lock(struct rpc_rqst *req, __be32 *p, struct nfs_lock_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_lock(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_lock(struct rpc_rqst *req, __be32 *p,
			      struct nfs_lock_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_lock(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a LOCKT request
 */
static int nfs4_xdr_enc_lockt(struct xdr_stream *xdr, struct nfs_lockt_args *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if(status)
		goto out;
	status = encode_lockt(xdr, args);
out:
	return status;
}

static int nfs40_xdr_enc_lockt(struct rpc_rqst *req, __be32 *p, struct nfs_lockt_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_lockt(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_lockt(struct rpc_rqst *req, __be32 *p,
			       struct nfs_lockt_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_lockt(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a LOCKU request
 */
static int nfs4_xdr_enc_locku(struct xdr_stream *xdr,
			      struct nfs_locku_args *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if(status)
		goto out;
	status = encode_locku(xdr, args);
out:
	return status;
}

static int nfs40_xdr_enc_locku(struct rpc_rqst *req, __be32 *p, struct nfs_locku_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_locku(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_locku(struct rpc_rqst *req, __be32 *p,
			       struct nfs_locku_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_locku(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a READLINK request
 */
static int nfs4_xdr_enc_readlink(struct rpc_rqst *req, struct xdr_stream *xdr,
					const struct nfs4_readlink *args,
					const size_t dec_readlink_sz)
{
	unsigned int replen;
	int status;
	struct rpc_auth *auth = req->rq_task->tk_msg.rpc_cred->cr_auth;

	status = encode_putfh(xdr, args->fh);
	if(status)
		goto out;
	status = encode_readlink(xdr, args, req);

	/* set up reply kvec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READLINK + status + string length = 8
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + dec_readlink_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, args->pages,
			args->pgbase, args->pglen);

out:
	return status;
}

static int nfs40_xdr_enc_readlink(struct rpc_rqst *req, __be32 *p, const struct nfs4_readlink *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_readlink(req, &xdr, args, NFS40_dec_readlink_sz);
}

# if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_readlink(struct rpc_rqst *req, __be32 *p,
				  const struct nfs4_readlink *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_readlink(req, &xdr, args, NFS41_dec_readlink_sz);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a READDIR request
 */
static int nfs4_xdr_enc_readdir(struct rpc_rqst *req, struct xdr_stream *xdr,
					const struct nfs4_readdir_arg *args,
					const size_t dec_readdir_sz)
{
	struct rpc_auth *auth = req->rq_task->tk_msg.rpc_cred->cr_auth;
	int replen;
	int status;

	status = encode_putfh(xdr, args->fh);
	if(status)
		goto out;
	status = encode_readdir(xdr, args, req);

	/* set up reply kvec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READDIR + status + verifer(2)  = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + dec_readdir_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, args->pages,
			 args->pgbase, args->count);
	dprintk("%s: inlined page args = (%u, %p, %u, %u)\n",
			__FUNCTION__, replen, args->pages,
			args->pgbase, args->count);

out:
	return status;
}

static int nfs40_xdr_enc_readdir(struct rpc_rqst *req, __be32 *p, const struct nfs4_readdir_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_readdir(req, &xdr, args, NFS40_dec_readdir_sz);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_readdir(struct rpc_rqst *req, __be32 *p,
				 const struct nfs4_readdir_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_readdir(req, &xdr, args, NFS41_dec_readdir_sz);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a READ request
 */
static int nfs4_xdr_enc_read(struct rpc_rqst *req, struct xdr_stream *xdr,
						struct nfs_readargs *args,
						const size_t dec_read_sz)
{
	struct rpc_auth *auth = req->rq_task->tk_msg.rpc_cred->cr_auth;
	int replen, status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_read(xdr, args);
	if (status)
		goto out;

	/* set up reply kvec
	 *    toplevel status + taglen=0 + rescount + OP_PUTFH + status
	 *       + OP_READ + status + eof + datalen = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + dec_read_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
			 args->pages, args->pgbase, args->count);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
out:
	return status;
}

static int nfs40_xdr_enc_read(struct rpc_rqst *req, __be32 *p, struct nfs_readargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_read(req, &xdr, args, NFS40_dec_read_sz);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_read(struct rpc_rqst *req, __be32 *p,
			      struct nfs_readargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_read(req, &xdr, args, NFS41_dec_read_sz);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode an SETATTR request
 */
static int nfs4_xdr_enc_setattr(struct xdr_stream *xdr, struct nfs_setattrargs *args)

{
        int status;

        status = encode_putfh(xdr, args->fh);
        if(status)
                goto out;
        status = encode_setattr(xdr, args, args->server);
        if(status)
                goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
        return status;
}

static int nfs40_xdr_enc_setattr(struct rpc_rqst *req, __be32 *p, struct nfs_setattrargs *args)

{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 3,
        };

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_setattr(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_setattr(struct rpc_rqst *req, __be32 *p,
				 struct nfs_setattrargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_setattr(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a GETACL request
 */
static int
nfs4_xdr_enc_getacl(struct rpc_rqst *req, struct xdr_stream *xdr,
		struct nfs_getaclargs *args, const size_t dec_getacl_sz)
{
	struct rpc_auth *auth = req->rq_task->tk_msg.rpc_cred->cr_auth;
	int replen, status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_getattr_two(xdr, FATTR4_WORD0_ACL, 0);
	/* set up reply buffer: */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + dec_getacl_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
		args->acl_pages, args->acl_pgbase, args->acl_len);
out:
	return status;
}

static int
nfs40_xdr_enc_getacl(struct rpc_rqst *req, __be32 *p,
		struct nfs_getaclargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_getacl(req, &xdr, args, NFS40_dec_getacl_sz);
}

#if defined(CONFIG_NFS_V4_1)
static int
nfs41_xdr_enc_getacl(struct rpc_rqst *req, __be32 *p,
		     struct nfs_getaclargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_getacl(req, &xdr, args, NFS41_dec_getacl_sz);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode a WRITE request
 */
static int nfs4_xdr_enc_write(struct xdr_stream *xdr, struct nfs_writeargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_write(xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_write(struct rpc_rqst *req, __be32 *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_write(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_write(struct rpc_rqst *req, __be32 *p,
			       struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_write(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 *  a COMMIT request
 */
static int nfs4_xdr_enc_commit(struct xdr_stream *xdr, struct nfs_writeargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status)
		goto out;
	status = encode_commit(xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_commit(struct rpc_rqst *req, __be32 *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_commit(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_commit(struct rpc_rqst *req, __be32 *p,
				struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_commit(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * FSINFO request
 */
static int nfs4_xdr_enc_fsinfo(struct xdr_stream *xdr, struct nfs4_fsinfo_arg *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (!status)
		status = encode_fsinfo(xdr, args->bitmask);
	return status;
}

static int nfs40_xdr_enc_fsinfo(struct rpc_rqst *req, __be32 *p, struct nfs4_fsinfo_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_fsinfo(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_fsinfo(struct rpc_rqst *req, __be32 *p,
				struct nfs4_fsinfo_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_fsinfo(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * a PATHCONF request
 */
static int nfs4_xdr_enc_pathconf(struct xdr_stream *xdr, const struct nfs4_pathconf_arg *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (!status)
		status = encode_getattr_one(xdr,
				args->bitmask[0] & nfs4_pathconf_bitmap[0]);
	return status;
}

static int nfs40_xdr_enc_pathconf(struct rpc_rqst *req, __be32 *p, const struct nfs4_pathconf_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_pathconf(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_pathconf(struct rpc_rqst *req, __be32 *p,
				  const struct nfs4_pathconf_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_pathconf(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * a STATFS request
 */
static int nfs4_xdr_enc_statfs(struct xdr_stream *xdr, const struct nfs4_statfs_arg *args)
{
	int status;

	status = encode_putfh(xdr, args->fh);
	if (status == 0)
		status = encode_getattr_two(xdr,
				args->bitmask[0] & nfs4_statfs_bitmap[0],
				args->bitmask[1] & nfs4_statfs_bitmap[1]);
	return status;
}

static int nfs40_xdr_enc_statfs(struct rpc_rqst *req, __be32 *p, const struct nfs4_statfs_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_statfs(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_statfs(struct rpc_rqst *req, __be32 *p,
				const struct nfs4_statfs_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_statfs(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * GETATTR_BITMAP request
 */
static int nfs4_xdr_enc_server_caps(struct xdr_stream *xdr, const struct nfs_fh *fhandle)
{
	int status;

	status = encode_putfh(xdr, fhandle);
	if (status == 0)
		status = encode_getattr_one(xdr, FATTR4_WORD0_SUPPORTED_ATTRS|
				FATTR4_WORD0_LINK_SUPPORT|
				FATTR4_WORD0_SYMLINK_SUPPORT|
				FATTR4_WORD0_ACLSUPPORT);
	return status;
}

static int nfs40_xdr_enc_server_caps(struct rpc_rqst *req, __be32 *p,
				     struct nfs4_server_caps_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_server_caps(&xdr, args->fhandle);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_server_caps(struct rpc_rqst *req, __be32 *p,
				     struct nfs4_server_caps_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_server_caps(&xdr, args->fhandle);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * a RENEW request
 */
static int nfs40_xdr_enc_renew(struct rpc_rqst *req, __be32 *p, struct nfs_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);
	return encode_renew(&xdr, clp);
}

/*
 * a SETCLIENTID request
 */
static int nfs40_xdr_enc_setclientid(struct rpc_rqst *req, __be32 *p, struct nfs4_setclientid *sc)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);
	return encode_setclientid(&xdr, sc);
}

/*
 * a SETCLIENTID_CONFIRM request
 */
static int nfs40_xdr_enc_setclientid_confirm(struct rpc_rqst *req, __be32 *p, struct nfs_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 3,
	};
	const u32 lease_bitmap[2] = { FATTR4_WORD0_LEASE_TIME, 0 };
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);
	status = encode_setclientid_confirm(&xdr, clp);
	if (!status)
		status = encode_putrootfh(&xdr);
	if (!status)
		status = encode_fsinfo(&xdr, lease_bitmap);
	return status;
}

/*
 * DELEGRETURN request
 */
static int nfs4_xdr_enc_delegreturn(struct xdr_stream *xdr, const struct nfs4_delegreturnargs *args)
{
	int status;

	status = encode_putfh(xdr, args->fhandle);
	if (status != 0)
		goto out;
	status = encode_delegreturn(xdr, args->stateid);
	if (status != 0)
		goto out;
	status = encode_getfattr(xdr, args->bitmask);
out:
	return status;
}

static int nfs40_xdr_enc_delegreturn(struct rpc_rqst *req, __be32 *p, const struct nfs4_delegreturnargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_delegreturn(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_delegreturn(struct rpc_rqst *req, __be32 *p,
				     const struct nfs4_delegreturnargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_delegreturn(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

#if defined(CONFIG_PNFS)
/*
 * Encode GETDEVICELIST request
 */
static int nfs41_xdr_enc_pnfs_getdevicelist(struct rpc_rqst *req, uint32_t *p,
				struct nfs4_pnfs_getdevicelist_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);
	status = encode_putfh(&xdr, args->fh);
	if (status != 0)
		goto out;
	status = encode_getdevicelist(&xdr, args);
out:
	return status;
}

/*
 * Encode GETDEVICEINFO request
 */
static int nfs41_xdr_enc_pnfs_getdeviceinfo(struct rpc_rqst *req,
				uint32_t *p,
				struct nfs4_pnfs_getdeviceinfo_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);
	return encode_getdeviceinfo(&xdr, args);
}

/*
 *  Encode LAYOUTGET request
 */
static int nfs41_xdr_enc_pnfs_layoutget(struct rpc_rqst *req, uint32_t *p,
					struct nfs4_pnfs_layoutget_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	status = encode_sequence(&xdr, &args->seq_args);
	if (status)
		goto out;
	status = encode_putfh(&xdr, NFS_FH(args->inode));
	if (status)
		goto out;
	status = encode_pnfs_layoutget(&xdr, args);
 out:
	return status;
}

/*
 * Encode LAYOUTRETURN request
 */
static int nfs41_xdr_enc_pnfs_layoutreturn(struct rpc_rqst *req, uint32_t *p,
					struct nfs4_pnfs_layoutreturn_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);
	status = encode_putfh(&xdr, NFS_FH(args->inode));
	if (status)
		goto out;
	status = encode_pnfs_layoutreturn(&xdr, args);
out:
	return status;
}

/*
 * Encode a PNFS WRITE request
 */
static int nfs41_xdr_enc_pnfs_write(struct rpc_rqst *req, uint32_t *p,
				    struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	status = encode_sequence(&xdr, &args->seq_args);
	if (status)
		goto out;
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_write(&xdr, args);
out:
	return status;
}

/*
 *  Encode LAYOUTCOMMIT request
 */
static int nfs41_xdr_enc_pnfs_layoutcommit(struct rpc_rqst *req, uint32_t *p,
					   struct pnfs_layoutcommit_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_pnfs_layoutcommit(&xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

#endif /* CONFIG_PNFS */

/*
 * Encode FS_LOCATIONS request
 */
static int nfs4_xdr_enc_fs_locations(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					struct nfs4_fs_locations_arg *args,
					unsigned int fsinfo_sz)
{
	struct rpc_auth *auth = req->rq_task->tk_msg.rpc_cred->cr_auth;
	int replen;
	int status;

	if ((status = encode_putfh(xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_lookup(xdr, args->name)) != 0)
		goto out;
	if ((status = encode_fs_locations(xdr, args->bitmask)) != 0)
		goto out;
	/* set up reply
	 *   toplevel_status + OP_PUTFH + status
	 *   + OP_LOOKUP + status + OP_GETATTR + status = 7
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + fsinfo_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, &args->page,
			0, PAGE_SIZE);
out:
	return status;
}

static int nfs40_xdr_enc_fs_locations(struct rpc_rqst *req, __be32 *p, struct nfs4_fs_locations_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_fs_locations(req, &xdr, args, NFS40_enc_fsinfo_sz);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_enc_fs_locations(struct rpc_rqst *req, __be32 *p,
				      struct nfs4_fs_locations_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_fs_locations(req, &xdr, args, NFS41_enc_fsinfo_sz);
}
#endif /* CONFIG_NFS_V4_1 */

#if defined(CONFIG_NFS_V4_1)
/*
 * EXCHANGE_ID request
 */
static int nfs41_xdr_enc_exchange_id(struct rpc_rqst *req, uint32_t *p,
				     void *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);

	encode_exchange_id(&xdr, args);

	return 0;
}

/*
 * a CREATE_SESSION request
 */
static int nfs41_xdr_enc_create_session(struct rpc_rqst *req, uint32_t *p,
					struct nfs41_create_session_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 1,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);

	status = encode_create_session(&xdr, args);
	return status;
}

/*
 * a DESTROY_SESSION request
 */
static int nfs41_xdr_enc_destroy_session(struct rpc_rqst *req, uint32_t *p,
					struct nfs4_session *session)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);

	encode_destroy_session(&xdr, session);

	return 0;
}

/*
 * a SEQUENCE request
 */
static int nfs41_xdr_enc_sequence(struct rpc_rqst *req, uint32_t *p,
				  struct nfs41_sequence_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	return encode_sequence(&xdr, args);
}

/*
 * a GET_LEASE_TIME request
 */
static int nfs41_xdr_enc_get_lease_time(struct rpc_rqst *req, uint32_t *p,
					struct nfs4_get_lease_time_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};
	int status;
	const u32 lease_bitmap[2] = { FATTR4_WORD0_LEASE_TIME, 0 };

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	status = encode_sequence(&xdr, &args->la_seq_args);
	if (status)
		goto out;
	status = encode_putrootfh(&xdr);
	if (status)
		goto out;
	status = encode_fsinfo(&xdr, lease_bitmap);
 out:
	return status;
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * START OF "GENERIC" DECODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define READTIME(x)       do {			\
	p++;					\
	(x.tv_sec) = ntohl(*p++);		\
	(x.tv_nsec) = ntohl(*p++);		\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

#define READ_BUF(nbytes)  do { \
	p = xdr_inline_decode(xdr, nbytes); \
	if (unlikely(!p)) { \
		dprintk("nfs: %s: prematurely hit end of receive" \
				" buffer\n", __FUNCTION__); \
		dprintk("nfs: %s: xdr->p=%p, bytes=%u, xdr->end=%p\n", \
				__FUNCTION__, xdr->p, nbytes, xdr->end); \
		return -EIO; \
	} \
} while (0)

static int decode_opaque_inline(struct xdr_stream *xdr, unsigned int *len, char **string)
{
	__be32 *p;

	READ_BUF(4);
	READ32(*len);
	READ_BUF(*len);
	*string = (char *)p;
	return 0;
}

static int decode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
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

static int decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	READ_BUF(8);
	READ32(opnum);
	if (opnum != expected) {
		dprintk("nfs: Server returned operation"
			" %d but we issued a request for %d\n",
				opnum, expected);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr != NFS_OK)
		return nfs4_stat_to_errno(nfserr);
	return 0;
}

/* Dummy routine */
static int decode_ace(struct xdr_stream *xdr, void *ace, struct nfs_client *clp)
{
	__be32 *p;
	unsigned int strlen;
	char *str;

	READ_BUF(12);
	return decode_opaque_inline(xdr, &strlen, &str);
}

static int decode_attr_bitmap(struct xdr_stream *xdr, uint32_t *bitmap)
{
	uint32_t bmlen;
	__be32 *p;

	READ_BUF(4);
	READ32(bmlen);

	bitmap[0] = bitmap[1] = 0;
	READ_BUF((bmlen << 2));
	if (bmlen > 0) {
		READ32(bitmap[0]);
		if (bmlen > 1)
			READ32(bitmap[1]);
	}
	return 0;
}

static inline int decode_attr_length(struct xdr_stream *xdr, uint32_t *attrlen, __be32 **savep)
{
	__be32 *p;

	READ_BUF(4);
	READ32(*attrlen);
	*savep = xdr->p;
	return 0;
}

static int decode_attr_supported(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *bitmask)
{
	if (likely(bitmap[0] & FATTR4_WORD0_SUPPORTED_ATTRS)) {
		decode_attr_bitmap(xdr, bitmask);
		bitmap[0] &= ~FATTR4_WORD0_SUPPORTED_ATTRS;
	} else
		bitmask[0] = bitmask[1] = 0;
	dprintk("%s: bitmask=%08x:%08x\n", __func__, bitmask[0], bitmask[1]);
	return 0;
}

static int decode_attr_type(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *type)
{
	__be32 *p;

	*type = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_TYPE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_TYPE)) {
		READ_BUF(4);
		READ32(*type);
		if (*type < NF4REG || *type > NF4NAMEDATTR) {
			dprintk("%s: bad type %d\n", __FUNCTION__, *type);
			return -EIO;
		}
		bitmap[0] &= ~FATTR4_WORD0_TYPE;
	}
	dprintk("%s: type=0%o\n", __FUNCTION__, nfs_type2fmt[*type].nfs2type);
	return 0;
}

static int decode_attr_change(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *change)
{
	__be32 *p;

	*change = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_CHANGE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_CHANGE)) {
		READ_BUF(8);
		READ64(*change);
		bitmap[0] &= ~FATTR4_WORD0_CHANGE;
	}
	dprintk("%s: change attribute=%Lu\n", __FUNCTION__,
			(unsigned long long)*change);
	return 0;
}

static int decode_attr_size(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *size)
{
	__be32 *p;

	*size = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_SIZE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_SIZE)) {
		READ_BUF(8);
		READ64(*size);
		bitmap[0] &= ~FATTR4_WORD0_SIZE;
	}
	dprintk("%s: file size=%Lu\n", __FUNCTION__, (unsigned long long)*size);
	return 0;
}

static int decode_attr_link_support(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_LINK_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_LINK_SUPPORT)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_LINK_SUPPORT;
	}
	dprintk("%s: link support=%s\n", __FUNCTION__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_symlink_support(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_SYMLINK_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_SYMLINK_SUPPORT)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_SYMLINK_SUPPORT;
	}
	dprintk("%s: symlink support=%s\n", __FUNCTION__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_fsid(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs_fsid *fsid)
{
	__be32 *p;

	fsid->major = 0;
	fsid->minor = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FSID - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FSID)) {
		READ_BUF(16);
		READ64(fsid->major);
		READ64(fsid->minor);
		bitmap[0] &= ~FATTR4_WORD0_FSID;
	}
	dprintk("%s: fsid=(0x%Lx/0x%Lx)\n", __FUNCTION__,
			(unsigned long long)fsid->major,
			(unsigned long long)fsid->minor);
	return 0;
}

static int decode_attr_lease_time(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = 60;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_LEASE_TIME - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_LEASE_TIME)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_LEASE_TIME;
	}
	dprintk("%s: file size=%u\n", __FUNCTION__, (unsigned int)*res);
	return 0;
}

static int decode_attr_aclsupport(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;

	*res = ACL4_SUPPORT_ALLOW_ACL|ACL4_SUPPORT_DENY_ACL;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACLSUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACLSUPPORT)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_ACLSUPPORT;
	}
	dprintk("%s: ACLs supported=%u\n", __FUNCTION__, (unsigned int)*res);
	return 0;
}

static int decode_attr_fileid(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *fileid)
{
	__be32 *p;

	*fileid = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILEID - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILEID)) {
		READ_BUF(8);
		READ64(*fileid);
		bitmap[0] &= ~FATTR4_WORD0_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __FUNCTION__, (unsigned long long)*fileid);
	return 0;
}

static int decode_attr_mounted_on_fileid(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *fileid)
{
	__be32 *p;

	*fileid = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_MOUNTED_ON_FILEID - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)) {
		READ_BUF(8);
		READ64(*fileid);
		bitmap[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __FUNCTION__, (unsigned long long)*fileid);
	return 0;
}

static int decode_attr_files_avail(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_AVAIL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_AVAIL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_AVAIL;
	}
	dprintk("%s: files avail=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_files_free(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_FREE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_FREE)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_FREE;
	}
	dprintk("%s: files free=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_files_total(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_TOTAL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_TOTAL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_TOTAL;
	}
	dprintk("%s: files total=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_pathname(struct xdr_stream *xdr, struct nfs4_pathname *path)
{
	u32 n;
	__be32 *p;
	int status = 0;

	READ_BUF(4);
	READ32(n);
	if (n == 0)
		goto root_path;
	dprintk("path ");
	path->ncomponents = 0;
	while (path->ncomponents < n) {
		struct nfs4_string *component = &path->components[path->ncomponents];
		status = decode_opaque_inline(xdr, &component->len, &component->data);
		if (unlikely(status != 0))
			goto out_eio;
		if (path->ncomponents != n)
			dprintk("/");
		dprintk("%s", component->data);
		if (path->ncomponents < NFS4_PATHNAME_MAXCOMPONENTS)
			path->ncomponents++;
		else {
			dprintk("cannot parse %d components in path\n", n);
			goto out_eio;
		}
	}
out:
	dprintk("\n");
	return status;
root_path:
/* a root pathname is sent as a zero component4 */
	path->ncomponents = 1;
	path->components[0].len=0;
	path->components[0].data=NULL;
	dprintk("path /\n");
	goto out;
out_eio:
	dprintk(" status %d", status);
	status = -EIO;
	goto out;
}

static int decode_attr_fs_locations(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs4_fs_locations *res)
{
	int n;
	__be32 *p;
	int status = -EIO;

	if (unlikely(bitmap[0] & (FATTR4_WORD0_FS_LOCATIONS -1U)))
		goto out;
	status = 0;
	if (unlikely(!(bitmap[0] & FATTR4_WORD0_FS_LOCATIONS)))
		goto out;
	dprintk("%s: fsroot ", __FUNCTION__);
	status = decode_pathname(xdr, &res->fs_path);
	if (unlikely(status != 0))
		goto out;
	READ_BUF(4);
	READ32(n);
	if (n <= 0)
		goto out_eio;
	res->nlocations = 0;
	while (res->nlocations < n) {
		u32 m;
		struct nfs4_fs_location *loc = &res->locations[res->nlocations];

		READ_BUF(4);
		READ32(m);

		loc->nservers = 0;
		dprintk("%s: servers ", __FUNCTION__);
		while (loc->nservers < m) {
			struct nfs4_string *server = &loc->servers[loc->nservers];
			status = decode_opaque_inline(xdr, &server->len, &server->data);
			if (unlikely(status != 0))
				goto out_eio;
			dprintk("%s ", server->data);
			if (loc->nservers < NFS4_FS_LOCATION_MAXSERVERS)
				loc->nservers++;
			else {
				unsigned int i;
				dprintk("%s: using first %u of %u servers "
					"returned for location %u\n",
						__FUNCTION__,
						NFS4_FS_LOCATION_MAXSERVERS,
						m, res->nlocations);
				for (i = loc->nservers; i < m; i++) {
					unsigned int len;
					char *data;
					status = decode_opaque_inline(xdr, &len, &data);
					if (unlikely(status != 0))
						goto out_eio;
				}
			}
		}
		status = decode_pathname(xdr, &loc->rootpath);
		if (unlikely(status != 0))
			goto out_eio;
		if (res->nlocations < NFS4_FS_LOCATIONS_MAXENTRIES)
			res->nlocations++;
	}
out:
	dprintk("%s: fs_locations done, error = %d\n", __FUNCTION__, status);
	return status;
out_eio:
	status = -EIO;
	goto out;
}

static int decode_attr_maxfilesize(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXFILESIZE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXFILESIZE)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_MAXFILESIZE;
	}
	dprintk("%s: maxfilesize=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_maxlink(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *maxlink)
{
	__be32 *p;
	int status = 0;

	*maxlink = 1;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXLINK - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXLINK)) {
		READ_BUF(4);
		READ32(*maxlink);
		bitmap[0] &= ~FATTR4_WORD0_MAXLINK;
	}
	dprintk("%s: maxlink=%u\n", __FUNCTION__, *maxlink);
	return status;
}

static int decode_attr_maxname(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *maxname)
{
	__be32 *p;
	int status = 0;

	*maxname = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXNAME - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXNAME)) {
		READ_BUF(4);
		READ32(*maxname);
		bitmap[0] &= ~FATTR4_WORD0_MAXNAME;
	}
	dprintk("%s: maxname=%u\n", __FUNCTION__, *maxname);
	return status;
}

static int decode_attr_maxread(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXREAD - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXREAD)) {
		uint64_t maxread;
		READ_BUF(8);
		READ64(maxread);
		if (maxread > 0x7FFFFFFF)
			maxread = 0x7FFFFFFF;
		*res = (uint32_t)maxread;
		bitmap[0] &= ~FATTR4_WORD0_MAXREAD;
	}
	dprintk("%s: maxread=%lu\n", __FUNCTION__, (unsigned long)*res);
	return status;
}

static int decode_attr_maxwrite(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXWRITE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXWRITE)) {
		uint64_t maxwrite;
		READ_BUF(8);
		READ64(maxwrite);
		if (maxwrite > 0x7FFFFFFF)
			maxwrite = 0x7FFFFFFF;
		*res = (uint32_t)maxwrite;
		bitmap[0] &= ~FATTR4_WORD0_MAXWRITE;
	}
	dprintk("%s: maxwrite=%lu\n", __FUNCTION__, (unsigned long)*res);
	return status;
}

static int decode_attr_mode(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *mode)
{
	__be32 *p;

	*mode = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_MODE - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_MODE)) {
		READ_BUF(4);
		READ32(*mode);
		*mode &= ~S_IFMT;
		bitmap[1] &= ~FATTR4_WORD1_MODE;
	}
	dprintk("%s: file mode=0%o\n", __FUNCTION__, (unsigned int)*mode);
	return 0;
}

static int decode_attr_nlink(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *nlink)
{
	__be32 *p;

	*nlink = 1;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_NUMLINKS - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_NUMLINKS)) {
		READ_BUF(4);
		READ32(*nlink);
		bitmap[1] &= ~FATTR4_WORD1_NUMLINKS;
	}
	dprintk("%s: nlink=%u\n", __FUNCTION__, (unsigned int)*nlink);
	return 0;
}

static int decode_attr_owner(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs_client *clp, uint32_t *uid)
{
	uint32_t len;
	__be32 *p;

	*uid = -2;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_OWNER)) {
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		if (len < XDR_MAX_NETOBJ) {
			if (nfs_map_name_to_uid(clp, (char *)p, len, uid) != 0)
				dprintk("%s: nfs_map_name_to_uid failed!\n",
						__FUNCTION__);
		} else
			dprintk("%s: name too long (%u)!\n",
					__FUNCTION__, len);
		bitmap[1] &= ~FATTR4_WORD1_OWNER;
	}
	dprintk("%s: uid=%d\n", __FUNCTION__, (int)*uid);
	return 0;
}

static int decode_attr_group(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs_client *clp, uint32_t *gid)
{
	uint32_t len;
	__be32 *p;

	*gid = -2;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER_GROUP - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_OWNER_GROUP)) {
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		if (len < XDR_MAX_NETOBJ) {
			if (nfs_map_group_to_gid(clp, (char *)p, len, gid) != 0)
				dprintk("%s: nfs_map_group_to_gid failed!\n",
						__FUNCTION__);
		} else
			dprintk("%s: name too long (%u)!\n",
					__FUNCTION__, len);
		bitmap[1] &= ~FATTR4_WORD1_OWNER_GROUP;
	}
	dprintk("%s: gid=%d\n", __FUNCTION__, (int)*gid);
	return 0;
}

static int decode_attr_rdev(struct xdr_stream *xdr, uint32_t *bitmap, dev_t *rdev)
{
	uint32_t major = 0, minor = 0;
	__be32 *p;

	*rdev = MKDEV(0,0);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_RAWDEV - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_RAWDEV)) {
		dev_t tmp;

		READ_BUF(8);
		READ32(major);
		READ32(minor);
		tmp = MKDEV(major, minor);
		if (MAJOR(tmp) == major && MINOR(tmp) == minor)
			*rdev = tmp;
		bitmap[1] &= ~ FATTR4_WORD1_RAWDEV;
	}
	dprintk("%s: rdev=(0x%x:0x%x)\n", __FUNCTION__, major, minor);
	return 0;
}

static int decode_attr_space_avail(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_AVAIL - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_AVAIL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_AVAIL;
	}
	dprintk("%s: space avail=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_free(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_FREE - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_FREE)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_FREE;
	}
	dprintk("%s: space free=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_total(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	__be32 *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_TOTAL - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_TOTAL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_TOTAL;
	}
	dprintk("%s: space total=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_used(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *used)
{
	__be32 *p;

	*used = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_USED - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_USED)) {
		READ_BUF(8);
		READ64(*used);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_USED;
	}
	dprintk("%s: space used=%Lu\n", __FUNCTION__,
			(unsigned long long)*used);
	return 0;
}

static int decode_attr_time(struct xdr_stream *xdr, struct timespec *time)
{
	__be32 *p;
	uint64_t sec;
	uint32_t nsec;

	READ_BUF(12);
	READ64(sec);
	READ32(nsec);
	time->tv_sec = (time_t)sec;
	time->tv_nsec = (long)nsec;
	return 0;
}

static int decode_attr_time_access(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_ACCESS - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_ACCESS)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_ACCESS;
	}
	dprintk("%s: atime=%ld\n", __FUNCTION__, (long)time->tv_sec);
	return status;
}

static int decode_attr_time_metadata(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_METADATA - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_METADATA)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_METADATA;
	}
	dprintk("%s: ctime=%ld\n", __FUNCTION__, (long)time->tv_sec);
	return status;
}

static int decode_attr_time_modify(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_MODIFY - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_MODIFY)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_MODIFY;
	}
	dprintk("%s: mtime=%ld\n", __FUNCTION__, (long)time->tv_sec);
	return status;
}

static int verify_attr_len(struct xdr_stream *xdr, __be32 *savep, uint32_t attrlen)
{
	unsigned int attrwords = XDR_QUADLEN(attrlen);
	unsigned int nwords = xdr->p - savep;

	if (unlikely(attrwords != nwords)) {
		dprintk("%s: server returned incorrect attribute length: "
			"%u %c %u\n",
				__FUNCTION__,
				attrwords << 2,
				(attrwords < nwords) ? '<' : '>',
				nwords << 2);
		return -EIO;
	}
	return 0;
}

#ifdef CONFIG_PNFS
/*
 * Decode potentially multiple layout types. Currently we only support
 * one layout driver per file system.
 */
static int decode_pnfs_list(struct xdr_stream *xdr, uint32_t *layoutclass)
{
	uint32_t *p;
	int num;

	READ_BUF(4);
	READ32(num);

	/* pNFS is not supported by the underlying file system */
	if (num == 0) {
		*layoutclass = 0;
		return 0;
	}

	/* TODO: We will eventually support multiple layout drivers ? */
	if (num > 1)
		printk(KERN_INFO "%s: Warning: Multiple pNFS layout drivers "
		       "per filesystem not supported\n", __func__);

	/* Decode and set first layout type */
	READ_BUF(num * 4);
	READ32(*layoutclass);
	return 0;
}

/*
 * The type of file system exported
 */
static int decode_attr_pnfstype(struct xdr_stream *xdr, uint32_t *bitmap,
				uint32_t *layoutclass)
{
	int status = 0;

	dprintk("%s: bitmap is %x\n", __func__, bitmap[1]);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_FS_LAYOUT_TYPES - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_FS_LAYOUT_TYPES)) {
		status = decode_pnfs_list(xdr, layoutclass);
		bitmap[1] &= ~FATTR4_WORD1_FS_LAYOUT_TYPES;
	}
	return status;
}

/*
 * Decode LAYOUTCOMMIT reply
 */
static int decode_pnfs_layoutcommit(struct xdr_stream *xdr,
				    struct rpc_rqst *req,
				    struct pnfs_layoutcommit_res *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_LAYOUTCOMMIT);
	if (status)
		return status;

	READ_BUF(4);
	READ32(res->sizechanged);

	if (res->sizechanged) {
		READ_BUF(8);
		READ64(res->newsize);
	}
	return 0;
}

#endif /* CONFIG_PNFS */

static int decode_change_info(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	__be32 *p;

	READ_BUF(20);
	READ32(cinfo->atomic);
	READ64(cinfo->before);
	READ64(cinfo->after);
	return 0;
}

static int decode_access(struct xdr_stream *xdr, struct nfs4_accessres *access)
{
	__be32 *p;
	uint32_t supp, acc;
	int status;

	status = decode_op_hdr(xdr, OP_ACCESS);
	if (status)
		return status;
	READ_BUF(8);
	READ32(supp);
	READ32(acc);
	access->supported = supp;
	access->access = acc;
	return 0;
}

static int decode_close(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_CLOSE);
	if (status)
		return status;
	READ_BUF(NFS4_STATEID_SIZE);
	COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
	return 0;
}

static int decode_commit(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_COMMIT);
	if (status)
		return status;
	READ_BUF(8);
	COPYMEM(res->verf->verifier, 8);
	return 0;
}

static int decode_create(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	__be32 *p;
	uint32_t bmlen;
	int status;

	status = decode_op_hdr(xdr, OP_CREATE);
	if (status)
		return status;
	if ((status = decode_change_info(xdr, cinfo)))
		return status;
	READ_BUF(4);
	READ32(bmlen);
	READ_BUF(bmlen << 2);
	return 0;
}

static int decode_server_caps(struct xdr_stream *xdr, struct nfs4_server_caps_res *res)
{
	__be32 *savep;
	uint32_t attrlen, 
		 bitmap[2] = {0};
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;
	if ((status = decode_attr_supported(xdr, bitmap, res->attr_bitmask)) != 0)
		goto xdr_error;
	if ((status = decode_attr_link_support(xdr, bitmap, &res->has_links)) != 0)
		goto xdr_error;
	if ((status = decode_attr_symlink_support(xdr, bitmap, &res->has_symlinks)) != 0)
		goto xdr_error;
	if ((status = decode_attr_aclsupport(xdr, bitmap, &res->acl_bitmask)) != 0)
		goto xdr_error;
	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}
	
static int decode_statfs(struct xdr_stream *xdr, struct nfs_fsstat *fsstat)
{
	__be32 *savep;
	uint32_t attrlen, 
		 bitmap[2] = {0};
	int status;
	
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	if ((status = decode_attr_files_avail(xdr, bitmap, &fsstat->afiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_files_free(xdr, bitmap, &fsstat->ffiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_files_total(xdr, bitmap, &fsstat->tfiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_avail(xdr, bitmap, &fsstat->abytes)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_free(xdr, bitmap, &fsstat->fbytes)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_total(xdr, bitmap, &fsstat->tbytes)) != 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}

static int decode_pathconf(struct xdr_stream *xdr, struct nfs_pathconf *pathconf)
{
	__be32 *savep;
	uint32_t attrlen, 
		 bitmap[2] = {0};
	int status;
	
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	if ((status = decode_attr_maxlink(xdr, bitmap, &pathconf->max_link)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxname(xdr, bitmap, &pathconf->max_namelen)) != 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}

static int decode_getfattr(struct xdr_stream *xdr, struct nfs_fattr *fattr, const struct nfs_server *server)
{
	__be32 *savep;
	uint32_t attrlen,
		 bitmap[2] = {0},
		 type;
	int status, fmode = 0;
	uint64_t fileid;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;

	fattr->bitmap[0] = bitmap[0];
	fattr->bitmap[1] = bitmap[1];

	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;


	if ((status = decode_attr_type(xdr, bitmap, &type)) != 0)
		goto xdr_error;
	fattr->type = nfs_type2fmt[type].nfs2type;
	fmode = nfs_type2fmt[type].mode;

	if ((status = decode_attr_change(xdr, bitmap, &fattr->change_attr)) != 0)
		goto xdr_error;
	if ((status = decode_attr_size(xdr, bitmap, &fattr->size)) != 0)
		goto xdr_error;
	if ((status = decode_attr_fsid(xdr, bitmap, &fattr->fsid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_fileid(xdr, bitmap, &fattr->fileid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_fs_locations(xdr, bitmap, container_of(fattr,
						struct nfs4_fs_locations,
						fattr))) != 0)
		goto xdr_error;
	if ((status = decode_attr_mode(xdr, bitmap, &fattr->mode)) != 0)
		goto xdr_error;
	fattr->mode |= fmode;
	if ((status = decode_attr_nlink(xdr, bitmap, &fattr->nlink)) != 0)
		goto xdr_error;
	if ((status = decode_attr_owner(xdr, bitmap, server->nfs_client, &fattr->uid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_group(xdr, bitmap, server->nfs_client, &fattr->gid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_rdev(xdr, bitmap, &fattr->rdev)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_used(xdr, bitmap, &fattr->du.nfs3.used)) != 0)
		goto xdr_error;
	if ((status = decode_attr_time_access(xdr, bitmap, &fattr->atime)) != 0)
		goto xdr_error;
	if ((status = decode_attr_time_metadata(xdr, bitmap, &fattr->ctime)) != 0)
		goto xdr_error;
	if ((status = decode_attr_time_modify(xdr, bitmap, &fattr->mtime)) != 0)
		goto xdr_error;
	if ((status = decode_attr_mounted_on_fileid(xdr, bitmap, &fileid)) != 0)
		goto xdr_error;
	if (fattr->fileid == 0 && fileid != 0)
		fattr->fileid = fileid;
	if ((status = verify_attr_len(xdr, savep, attrlen)) == 0)
		fattr->valid = NFS_ATTR_FATTR | NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4;
xdr_error:
	dprintk("%s: xdr returned %d\n", __FUNCTION__, -status);
	return status;
}


static int decode_fsinfo(struct xdr_stream *xdr, struct nfs_fsinfo *fsinfo)
{
	__be32 *savep;
	uint32_t attrlen, bitmap[2];
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	fsinfo->rtmult = fsinfo->wtmult = 512;	/* ??? */

	if ((status = decode_attr_lease_time(xdr, bitmap, &fsinfo->lease_time)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxfilesize(xdr, bitmap, &fsinfo->maxfilesize)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxread(xdr, bitmap, &fsinfo->rtmax)) != 0)
		goto xdr_error;
	fsinfo->rtpref = fsinfo->dtpref = fsinfo->rtmax;
	if ((status = decode_attr_maxwrite(xdr, bitmap, &fsinfo->wtmax)) != 0)
		goto xdr_error;
	fsinfo->wtpref = fsinfo->wtmax;
#ifdef CONFIG_PNFS
	status = decode_attr_pnfstype(xdr, bitmap, &fsinfo->layoutclass);
	if (status)
		goto xdr_error;
#endif /* CONFIG_PNFS */

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}

static int decode_getfh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;
	uint32_t len;
	int status;

	/* Zero handle first to allow comparisons */
	memset(fh, 0, sizeof(*fh));

	status = decode_op_hdr(xdr, OP_GETFH);
	if (status)
		return status;

	READ_BUF(4);
	READ32(len);
	if (len > NFS4_FHSIZE)
		return -EIO;
	fh->size = len;
	READ_BUF(len);
	COPYMEM(fh->data, len);
	return 0;
}

static int decode_link(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	int status;
	
	status = decode_op_hdr(xdr, OP_LINK);
	if (status)
		return status;
	return decode_change_info(xdr, cinfo);
}

/*
 * We create the owner, so we know a proper owner.id length is 4.
 */
static int decode_lock_denied (struct xdr_stream *xdr, struct file_lock *fl)
{
	uint64_t offset, length, clientid;
	__be32 *p;
	uint32_t namelen, type;

	READ_BUF(32);
	READ64(offset);
	READ64(length);
	READ32(type);
	if (fl != NULL) {
		fl->fl_start = (loff_t)offset;
		fl->fl_end = fl->fl_start + (loff_t)length - 1;
		if (length == ~(uint64_t)0)
			fl->fl_end = OFFSET_MAX;
		fl->fl_type = F_WRLCK;
		if (type & 1)
			fl->fl_type = F_RDLCK;
		fl->fl_pid = 0;
	}
	READ64(clientid);
	READ32(namelen);
	READ_BUF(namelen);
	return -NFS4ERR_DENIED;
}

static int decode_lock(struct xdr_stream *xdr, struct nfs_lock_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_LOCK);
	if (status == 0) {
		READ_BUF(NFS4_STATEID_SIZE);
		COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
	} else if (status == -NFS4ERR_DENIED)
		return decode_lock_denied(xdr, NULL);
	return status;
}

static int decode_lockt(struct xdr_stream *xdr, struct nfs_lockt_res *res)
{
	int status;
	status = decode_op_hdr(xdr, OP_LOCKT);
	if (status == -NFS4ERR_DENIED)
		return decode_lock_denied(xdr, res->denied);
	return status;
}

static int decode_locku(struct xdr_stream *xdr, struct nfs_locku_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_LOCKU);
	if (status == 0) {
		READ_BUF(NFS4_STATEID_SIZE);
		COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
	}
	return status;
}

static int decode_lookup(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LOOKUP);
}

/* This is too sick! */
static int decode_space_limit(struct xdr_stream *xdr, u64 *maxsize)
{
        __be32 *p;
	uint32_t limit_type, nblocks, blocksize;

	READ_BUF(12);
	READ32(limit_type);
	switch (limit_type) {
		case 1:
			READ64(*maxsize);
			break;
		case 2:
			READ32(nblocks);
			READ32(blocksize);
			*maxsize = (uint64_t)nblocks * (uint64_t)blocksize;
	}
	return 0;
}

static int decode_delegation(struct xdr_stream *xdr, struct nfs_openres *res)
{
        __be32 *p;
        uint32_t delegation_type;

	READ_BUF(4);
	READ32(delegation_type);
	if (delegation_type == NFS4_OPEN_DELEGATE_NONE) {
		res->delegation_type = 0;
		return 0;
	}
	READ_BUF(NFS4_STATEID_SIZE+4);
	COPYMEM(res->delegation.data, NFS4_STATEID_SIZE);
	READ32(res->do_recall);
	switch (delegation_type) {
		case NFS4_OPEN_DELEGATE_READ:
			res->delegation_type = FMODE_READ;
			break;
		case NFS4_OPEN_DELEGATE_WRITE:
			res->delegation_type = FMODE_WRITE|FMODE_READ;
			if (decode_space_limit(xdr, &res->maxsize) < 0)
				return -EIO;
	}
	return decode_ace(xdr, NULL, res->server->nfs_client);
}

static int decode_open(struct xdr_stream *xdr, struct nfs_openres *res)
{
        __be32 *p;
	uint32_t savewords, bmlen, i;
        int status;

        status = decode_op_hdr(xdr, OP_OPEN);
        if (status)
                return status;
        READ_BUF(NFS4_STATEID_SIZE);
        COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);

        decode_change_info(xdr, &res->cinfo);

        READ_BUF(8);
        READ32(res->rflags);
        READ32(bmlen);
        if (bmlen > 10)
                goto xdr_error;

        READ_BUF(bmlen << 2);
	savewords = min_t(uint32_t, bmlen, NFS4_BITMAP_SIZE);
	for (i = 0; i < savewords; ++i)
		READ32(res->attrset[i]);
	for (; i < NFS4_BITMAP_SIZE; i++)
		res->attrset[i] = 0;

	return decode_delegation(xdr, res);
xdr_error:
	dprintk("%s: Bitmap too large! Length = %u\n", __FUNCTION__, bmlen);
	return -EIO;
}

static int decode_open_confirm(struct xdr_stream *xdr, struct nfs_open_confirmres *res)
{
        __be32 *p;
	int status;

        status = decode_op_hdr(xdr, OP_OPEN_CONFIRM);
        if (status)
                return status;
        READ_BUF(NFS4_STATEID_SIZE);
        COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
        return 0;
}

static int decode_open_downgrade(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_OPEN_DOWNGRADE);
	if (status)
		return status;
	READ_BUF(NFS4_STATEID_SIZE);
	COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
	return 0;
}

static int decode_putfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTFH);
}

static int decode_putrootfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTROOTFH);
}

static int decode_read(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs_readres *res)
{
	struct kvec *iov = req->rq_rcv_buf.head;
	__be32 *p;
	uint32_t count, eof, recvd, hdrlen;
	int status;

	status = decode_op_hdr(xdr, OP_READ);
	if (status)
		return status;
	READ_BUF(8);
	READ32(eof);
	READ32(count);
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (count > recvd) {
		dprintk("NFS: server cheating in read reply: "
				"count %u > recvd %u\n", count, recvd);
		count = recvd;
		eof = 0;
	}
	xdr_read_pages(xdr, count);
	res->eof = eof;
	res->count = count;
	return 0;
}

static int decode_readdir(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs4_readdir_res *readdir)
{
	struct xdr_buf	*rcvbuf = &req->rq_rcv_buf;
	struct page	*page = *rcvbuf->pages;
	struct kvec	*iov = rcvbuf->head;
	size_t		hdrlen;
	u32		recvd, pglen = rcvbuf->page_len;
	__be32		*end, *entry, *p, *kaddr;
	unsigned int	nr;
	int		status;

	status = decode_op_hdr(xdr, OP_READDIR);
	if (status)
		return status;
	READ_BUF(8);
	COPYMEM(readdir->verifier.data, 8);
	dprintk("%s: verifier = %08x:%08x\n",
			__func__,
			((u32 *)readdir->verifier.data)[0],
			((u32 *)readdir->verifier.data)[1]);


	hdrlen = (char *) p - (char *) iov->iov_base;
	recvd = rcvbuf->len - hdrlen;
	if (pglen > recvd)
		pglen = recvd;
	xdr_read_pages(xdr, pglen);

	BUG_ON(pglen + readdir->pgbase > PAGE_CACHE_SIZE);
	kaddr = p = kmap_atomic(page, KM_USER0);
	end = p + ((pglen + readdir->pgbase) >> 2);
	entry = p;
	for (nr = 0; *p++; nr++) {
		u32 len, attrlen, xlen;
		if (end - p < 3)
			goto short_pkt;
		dprintk("cookie = %Lu, ", *((unsigned long long *)p));
		p += 2;			/* cookie */
		len = ntohl(*p++);	/* filename length */
		if (len > NFS4_MAXNAMLEN) {
			dprintk("NFS: giant filename in readdir (len 0x%x)\n",
					len);
			goto err_unmap;
		}
		xlen = XDR_QUADLEN(len);
		if (end - p < xlen + 1)
			goto short_pkt;
		dprintk("filename = %*s\n", len, (char *)p);
		p += xlen;
		len = ntohl(*p++);	/* bitmap length */
		if (end - p < len + 1)
			goto short_pkt;
		p += len;
		attrlen = XDR_QUADLEN(ntohl(*p++));
		if (end - p < attrlen + 2)
			goto short_pkt;
		p += attrlen;		/* attributes */
		entry = p;
	}
	if (!nr && (entry[0] != 0 || entry[1] == 0))
		goto short_pkt;
out:	
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
short_pkt:
	dprintk("%s: short packet at entry %d\n", __FUNCTION__, nr);
	entry[0] = entry[1] = 0;
	/* truncate listing ? */
	if (!nr) {
		dprintk("NFS: readdir reply truncated!\n");
		entry[1] = 1;
	}
	goto out;
err_unmap:
	kunmap_atomic(kaddr, KM_USER0);
	return -errno_NFSERR_IO;
}

static int decode_readlink(struct xdr_stream *xdr, struct rpc_rqst *req)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	size_t hdrlen;
	u32 len, recvd;
	__be32 *p;
	char *kaddr;
	int status;

	status = decode_op_hdr(xdr, OP_READLINK);
	if (status)
		return status;

	/* Convert length of symlink */
	READ_BUF(4);
	READ32(len);
	if (len >= rcvbuf->page_len || len <= 0) {
		dprintk("nfs: server returned giant symlink!\n");
		return -ENAMETOOLONG;
	}
	hdrlen = (char *) xdr->p - (char *) iov->iov_base;
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (recvd < len) {
		dprintk("NFS: server cheating in readlink reply: "
				"count %u > recvd %u\n", len, recvd);
		return -EIO;
	}
	xdr_read_pages(xdr, len);
	/*
	 * The XDR encode routine has set things up so that
	 * the link text will be copied directly into the
	 * buffer.  We just have to do overflow-checking,
	 * and and null-terminate the text (the VFS expects
	 * null-termination).
	 */
	kaddr = (char *)kmap_atomic(rcvbuf->pages[0], KM_USER0);
	kaddr[len+rcvbuf->page_base] = '\0';
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
}

static int decode_remove(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_REMOVE);
	if (status)
		goto out;
	status = decode_change_info(xdr, cinfo);
out:
	return status;
}

static int decode_rename(struct xdr_stream *xdr, struct nfs4_change_info *old_cinfo,
	      struct nfs4_change_info *new_cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_RENAME);
	if (status)
		goto out;
	if ((status = decode_change_info(xdr, old_cinfo)))
		goto out;
	status = decode_change_info(xdr, new_cinfo);
out:
	return status;
}

static int decode_renew(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RENEW);
}

static int
decode_restorefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RESTOREFH);
}

static int decode_getacl(struct xdr_stream *xdr, struct rpc_rqst *req,
		size_t *acl_len)
{
	__be32 *savep;
	uint32_t attrlen,
		 bitmap[2] = {0};
	struct kvec *iov = req->rq_rcv_buf.head;
	int status;

	*acl_len = 0;
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto out;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto out;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto out;

	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACL)) {
		size_t hdrlen;
		u32 recvd;

		/* We ignore &savep and don't do consistency checks on
		 * the attr length.  Let userspace figure it out.... */
		hdrlen = (u8 *)xdr->p - (u8 *)iov->iov_base;
		recvd = req->rq_rcv_buf.len - hdrlen;
		if (attrlen > recvd) {
			dprintk("NFS: server cheating in getattr"
					" acl reply: attrlen %u > recvd %u\n",
					attrlen, recvd);
			return -EINVAL;
		}
		xdr_read_pages(xdr, attrlen);
		*acl_len = attrlen;
	} else
		status = -EOPNOTSUPP;

out:
	return status;
}

static int
decode_savefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SAVEFH);
}

static int decode_setattr(struct xdr_stream *xdr, struct nfs_setattrres *res)
{
	__be32 *p;
	uint32_t bmlen;
	int status;

        
	status = decode_op_hdr(xdr, OP_SETATTR);
	if (status)
		return status;
	READ_BUF(4);
	READ32(bmlen);
	READ_BUF(bmlen << 2);
	return 0;
}

static int decode_setclientid(struct xdr_stream *xdr, struct nfs_client *clp)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	READ_BUF(8);
	READ32(opnum);
	if (opnum != OP_SETCLIENTID) {
		dprintk("nfs: decode_setclientid: Server returned operation"
			       	" %d\n", opnum);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr == NFS_OK) {
		READ_BUF(8 + NFS4_VERIFIER_SIZE);
		READ64(clp->cl_clientid);
		COPYMEM(clp->cl_confirm.data, NFS4_VERIFIER_SIZE);
	} else if (nfserr == NFSERR_CLID_INUSE) {
		uint32_t len;

		/* skip netid string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);

		/* skip uaddr string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		return -NFSERR_CLID_INUSE;
	} else
		return nfs4_stat_to_errno(nfserr);

	return 0;
}

static int decode_setclientid_confirm(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM);
}

static int decode_write(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_WRITE);
	if (status)
		return status;

	READ_BUF(16);
	READ32(res->count);
	READ32(res->verf->committed);
	COPYMEM(res->verf->verifier, 8);
	return 0;
}

static int decode_delegreturn(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_DELEGRETURN);
}

#if defined(CONFIG_NFS_V4_1)
static int decode_exchange_id(struct xdr_stream *xdr,
			      struct nfs41_exchange_id_res *res)
{
	uint32_t *p;
	int status, dummy;
	struct nfs_client *clp = res->client;

	status = decode_op_hdr(xdr, OP_EXCHANGE_ID);
	if (status)
		return status;

	READ_BUF(8);
	READ64(clp->cl_clientid);
	READ_BUF(12);
	READ32(clp->cl_seqid);
	READ32(clp->cl_exchange_flags);

	/* We ask for SP4_NONE */
	READ32(dummy);
	if (dummy != SP4_NONE)
		return -EIO;

	/* minor_id */
	READ_BUF(8);
	READ64(res->server_owner.minor_id);

	/* Major id */
	READ_BUF(4);
	READ32(res->server_owner.major_id_sz);
	READ_BUF(res->server_owner.major_id_sz);
	COPYMEM(res->server_owner.major_id, res->server_owner.major_id_sz);

	/* server_scope */
	READ_BUF(4);
	READ32(res->server_scope.server_scope_sz);
	READ_BUF(res->server_scope.server_scope_sz);
	COPYMEM(res->server_scope.server_scope,
		res->server_scope.server_scope_sz);
	/* Throw away Implementation id array */
	READ_BUF(4);
	READ32(dummy);
	p += XDR_QUADLEN(dummy);

	return 0;
}

static int decode_create_session(struct xdr_stream *xdr,
				 struct nfs41_create_session_res *res)
{
	uint32_t *p;
	int status;
	u32 nr_attrs;

	struct nfs4_session *session = res->session;
	struct nfs_client *clp = res->client;

	status = decode_op_hdr(xdr, OP_CREATE_SESSION);

	if (status)
		return status;

	/* sessionid */
	READ_BUF(NFS4_MAX_SESSIONID_LEN);
	COPYMEM(&session->sess_id, NFS4_MAX_SESSIONID_LEN);

	/* seqid, flags */
	READ_BUF(8);
	READ32(clp->cl_seqid);
	READ32(session->flags);

	/* Channel attributes */
	/* fore channel */
	READ_BUF(24);
	READ32(session->fore_channel.chan_attrs.headerpadsz);
	READ32(session->fore_channel.chan_attrs.max_rqst_sz);
	READ32(session->fore_channel.chan_attrs.max_resp_sz);
	READ32(session->fore_channel.chan_attrs.max_resp_sz_cached);
	READ32(session->fore_channel.chan_attrs.max_ops);
	READ32(session->fore_channel.chan_attrs.max_reqs);
	READ_BUF(4);
	READ32(nr_attrs);
	if (nr_attrs == 1) {
		READ_BUF(4);
		READ32(session->fore_channel.chan_attrs.rdma_attrs);
	}

	/* back channel */
	READ_BUF(24);
	READ32(session->fore_channel.chan_attrs.headerpadsz);
	READ32(session->back_channel.chan_attrs.max_rqst_sz);
	READ32(session->back_channel.chan_attrs.max_resp_sz);
	READ32(session->back_channel.chan_attrs.max_resp_sz_cached);
	READ32(session->back_channel.chan_attrs.max_ops);
	READ32(session->back_channel.chan_attrs.max_reqs);
	READ_BUF(4);
	READ32(nr_attrs);
	if (nr_attrs == 1) {
		READ_BUF(4);
		READ32(session->back_channel.chan_attrs.rdma_attrs);
	}

	return 0;
}

static int decode_destroy_session(struct xdr_stream *xdr, void *dummy)
{
	return decode_op_hdr(xdr, OP_DESTROY_SESSION);
}

static int decode_sequence(struct xdr_stream *xdr,
			   struct nfs41_sequence_res *res)
{
	__be32 *p;
	int status;

	status = decode_op_hdr(xdr, OP_SEQUENCE);
	if (status)
		return status;

	READ_BUF(NFS4_MAX_SESSIONID_LEN + 20);
	COPYMEM(res->sr_sessionid.data, NFS4_MAX_SESSIONID_LEN);
	READ32(res->sr_seqid);
	READ32(res->sr_slotid);
	READ32(res->sr_max_slotid);
	READ32(res->sr_target_max_slotid);
	READ32(res->sr_flags);

	return 0;
}
#endif /* CONFIG_NFS_V4_1 */

#ifdef CONFIG_PNFS
/*
 * Decode getdevicelist results for pNFS.
 * TODO: Need to handle case when EOF != true;
 */
static int decode_getdevicelist(struct xdr_stream *xdr,
				struct pnfs_devicelist *res)
{
	__be32 *p;
	int status, i;
	struct nfs_writeverf verftemp;

	status = decode_op_hdr(xdr, OP_GETDEVICELIST);
	if (status)
		return status;

	/* TODO: Skip cookie for now */
	READ_BUF(8);
	(*p) += 2;

	/* Read verifier */
	READ_BUF(8);
	COPYMEM(verftemp.verifier, 8);

	READ_BUF(4);
	READ32(res->num_devs);

	dprintk("%s: num_dev %d \n", __func__, res->num_devs);

	if (res->num_devs > NFS4_PNFS_DEV_MAXNUM)
		return -NFS4ERR_REP_TOO_BIG;

	for (i = 0; i < res->num_devs; i++) {
		READ_BUF(NFS4_PNFS_DEVICEID4_SIZE);
		COPYMEM(res->dev_id[i].data, NFS4_PNFS_DEVICEID4_SIZE);
	}
	READ_BUF(4);
	READ32(res->eof);
	return 0;
}

/*
 * Decode GETDEVICEINFO reply
 */
static int decode_getdeviceinfo(struct xdr_stream *xdr,
				struct pnfs_device *res)
{
	__be32 *p;
	uint32_t len, type, tlen, mincount;
	int status;

	status = decode_op_hdr(xdr, OP_GETDEVICEINFO);
	if (status) {
		/* TODO: Do we want to resend getdeviceinfo with mincount? */
		if (status == -NFS4ERR_TOOSMALL) {
			READ_BUF(4);
			READ32(mincount);
			dprintk("%s: Min count too small. mincnt = %u\n",
			       __func__, mincount);
		}
		return status;
	}

	READ_BUF(8);
	READ32(type);
	if (type != res->layout_type) {
		dprintk("%s: layout mismatch req: %u res: %u\n",
			__func__, res->layout_type, type);
		return -EINVAL;
	}
	READ32(len);
	READ_BUF(len);

	COPYMEM(&res->dev_addr_buf, len);
	res->dev_addr_len = len;

	/* At most one bitmap word */
	READ_BUF(4);
	READ32(tlen);
	if (tlen) {
		READ_BUF(4);
		READ32(res->dev_notify_types);
	} else
		res->dev_notify_types = 0;
	return 0;
}

/*
 * Decode LAYOUT_GET reply
 */
static int decode_pnfs_layoutget(struct xdr_stream *xdr, struct rpc_rqst *req,
				 struct nfs4_pnfs_layoutget_res *res)
{
	uint32_t *p;
	int status;
	u32 layout_count;

	status = decode_op_hdr(xdr, OP_LAYOUTGET);
	if (status)
		return status;
	READ_BUF(8 + NFS4_STATEID_SIZE);
	READ32(res->return_on_close);
	COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
	READ32(layout_count);
	if (!layout_count) {
		dprintk("%s: server responded with empty layout array\n", __func__);
		return -EINVAL;
	}
	/* FIXME: the whole layotu array should be passed up to the pnfs client */
	if (layout_count > 1)
		dprintk("%s: server responded with %d layouts, dropping tail\n",
			__func__, layout_count);
	READ_BUF(28 * layout_count);
	READ64(res->lseg.offset);
	READ64(res->lseg.length);
	READ32(res->lseg.iomode);
	READ32(res->type);
	READ32(res->layout.len);

	dprintk("%s roff:%lu rlen:%lu riomode:%d, lo_type:0x%x, lo.len:%d\n",
		__func__,
		(unsigned long)res->lseg.offset,
		(unsigned long)res->lseg.length,
		res->lseg.iomode,
		res->type,
		res->layout.len);

	res->layout.buf = kmalloc(res->layout.len, GFP_KERNEL);
	if (!res->layout.buf)
		return -ENOMEM;
	READ_BUF(res->layout.len);
	COPYMEM(res->layout.buf, res->layout.len);
	return 0;
}
/*
 * Decode LAYOUTRETURN reply
 */
static int decode_pnfs_layoutreturn(struct xdr_stream *xdr, uint32_t *p, struct nfs4_pnfs_layoutreturn_res *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_LAYOUTRETURN);
	if (status)
		return status;
	READ_BUF(4);
	READ32(res->lrs_present);
	if (res->lrs_present) {
		READ_BUF(NFS4_STATEID_SIZE);
		COPYMEM(res->stateid.data, NFS4_STATEID_SIZE);
	}
	return 0;
}

 #endif /* CONFIG_PNFS */

/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

#if defined(CONFIG_NFS_V4_1)
static void nfs41_xdr_dec_error(struct rpc_rqst *req, __be32 *p, void *res)
{
	BUG();
}
#endif /* CONFIG_NFS_V4_1 */

static inline int nfs4_fixup_status(int status, int hdr_status)
{
	if (likely(!status))
		return 0;
	return nfs4_stat_to_errno(hdr_status);
}

/*
 * Decode OPEN_DOWNGRADE response
 */
static int nfs4_xdr_dec_open_downgrade(struct xdr_stream *xdr, struct nfs_closeres *res)
{
        int status;

        status = decode_putfh(xdr);
        if (status)
                goto out;
        status = decode_open_downgrade(xdr, res);
	if (status != 0)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_open_downgrade(struct rpc_rqst *rqstp, __be32 *p, struct nfs_closeres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = nfs4_xdr_dec_open_downgrade(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_open_downgrade(struct rpc_rqst *rqstp, __be32 *p,
					struct nfs_closeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_open_downgrade(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);

}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode ACCESS response
 */
static int nfs4_xdr_dec_access(struct xdr_stream *xdr, struct nfs4_accessres *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status != 0)
		goto out;
	status = decode_access(xdr, res);
	if (status != 0)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_access(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_accessres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_access(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_access(struct rpc_rqst *rqstp, __be32 *p,
				struct nfs4_accessres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;

	status = nfs4_xdr_dec_access(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode LOOKUP response
 */
static int nfs4_xdr_dec_lookup(struct xdr_stream *xdr, struct nfs4_lookup_res *res)
{
	int status;
	
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_lookup(xdr)) != 0)
		goto out;
	if ((status = decode_getfh(xdr, res->fh)) != 0)
		goto out;
	status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_lookup(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_lookup(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_lookup(struct rpc_rqst *rqstp, __be32 *p,
				struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;

	status = nfs4_xdr_dec_lookup(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode LOOKUP_ROOT response
 */
static int nfs4_xdr_dec_lookup_root(struct xdr_stream *xdr, struct nfs4_lookup_res *res)
{
	int status;
	
	if ((status = decode_putrootfh(xdr)) != 0)
		goto out;
	if ((status = decode_getfh(xdr, res->fh)) == 0)
		status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_lookup_root(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_lookup_root(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_lookup_root(struct rpc_rqst *rqstp, __be32 *p,
				     struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;

	status = nfs4_xdr_dec_lookup_root(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode REMOVE response
 */
static int nfs4_xdr_dec_remove(struct xdr_stream *xdr, struct nfs_removeres *res)
{
	int status;
	
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_remove(xdr, &res->cinfo)) != 0)
		goto out;
	decode_getfattr(xdr, &res->dir_attr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_remove(struct rpc_rqst *rqstp, __be32 *p, struct nfs_removeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	status = nfs4_xdr_dec_remove(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_remove(struct rpc_rqst *rqstp, __be32 *p,
				struct nfs_removeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_remove(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode RENAME response
 */
static int nfs4_xdr_dec_rename(struct xdr_stream *xdr, struct nfs4_rename_res *res)
{
	int status;
	
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_savefh(xdr)) != 0)
		goto out;
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_rename(xdr, &res->old_cinfo, &res->new_cinfo)) != 0)
		goto out;
	/* Current FH is target directory */
	if (decode_getfattr(xdr, res->new_fattr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(xdr)) != 0)
		goto out;
	decode_getfattr(xdr, res->old_fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_rename(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_rename_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_rename(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_rename(struct rpc_rqst *rqstp, __be32 *p,
				struct nfs4_rename_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_rename(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode LINK response
 */
static int nfs4_xdr_dec_link(struct xdr_stream *xdr, struct nfs4_link_res *res)
{
	int status;
	
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_savefh(xdr)) != 0)
		goto out;
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_link(xdr, &res->cinfo)) != 0)
		goto out;
	/*
	 * Note order: OP_LINK leaves the directory as the current
	 *             filehandle.
	 */
	if (decode_getfattr(xdr, res->dir_attr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(xdr)) != 0)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_link(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_link_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_link(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_link(struct rpc_rqst *rqstp, __be32 *p,
			      struct nfs4_link_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_link(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode CREATE response
 */
static int nfs4_xdr_dec_create(struct xdr_stream *xdr, struct nfs4_create_res *res)
{
	int status;
	
	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_savefh(xdr)) != 0)
		goto out;
	if ((status = decode_create(xdr,&res->dir_cinfo)) != 0)
		goto out;
	if ((status = decode_getfh(xdr, res->fh)) != 0)
		goto out;
	if (decode_getfattr(xdr, res->fattr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(xdr)) != 0)
		goto out;
	decode_getfattr(xdr, res->dir_fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_create(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_create_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_create(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_create(struct rpc_rqst *rqstp, __be32 *p,
				struct nfs4_create_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_create(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode SYMLINK response
 */
static int nfs40_xdr_dec_symlink(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_create_res *res)
{
	return nfs40_xdr_dec_create(rqstp, p, res);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_symlink(struct rpc_rqst *rqstp, __be32 *p,
				 struct nfs4_create_res *res)
{
	return nfs41_xdr_dec_create(rqstp, p, res);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode GETATTR response
 */
static int nfs4_xdr_dec_getattr(struct xdr_stream *xdr, struct nfs4_getattr_res *res)
{
	int status;
	
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_getattr(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_getattr_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_getattr(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);

}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_getattr(struct rpc_rqst *rqstp, __be32 *p,
				 struct nfs4_getattr_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_getattr(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);

}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Encode an SETACL request
 */
static int
nfs4_xdr_enc_setacl(struct xdr_stream *xdr, struct nfs_setaclargs *args)
{
        int status;

        status = encode_putfh(xdr, args->fh);
        if (status)
                goto out;
        status = encode_setacl(xdr, args);
out:
	return status;
}

static int
nfs40_xdr_enc_setacl(struct rpc_rqst *req, __be32 *p, struct nfs_setaclargs *args)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 2,
        };

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 0);

	return nfs4_xdr_enc_setacl(&xdr, args);
}

#if defined(CONFIG_NFS_V4_1)
static int
nfs41_xdr_enc_setacl(struct rpc_rqst *req, __be32 *p,
		     struct nfs_setaclargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr, 1);
	encode_sequence(&xdr, &args->seq_args);

	return nfs4_xdr_enc_setacl(&xdr, args);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode SETACL response
 */
static int
nfs4_xdr_dec_setacl(struct xdr_stream *xdr, void *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_setattr(xdr, res);
out:
	return status;
}

static int
nfs40_xdr_dec_setacl(struct rpc_rqst *rqstp, __be32 *p, void *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_setacl(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int
nfs41_xdr_dec_setacl(struct rpc_rqst *rqstp, __be32 *p,
		     struct nfs_setaclres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_setacl(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode GETACL response
 */
static int
nfs4_xdr_dec_getacl(struct rpc_rqst *rqstp, struct xdr_stream *xdr, size_t *acl_len)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_getacl(xdr, rqstp, acl_len);

out:
	return status;
}

static int
nfs40_xdr_dec_getacl(struct rpc_rqst *rqstp, __be32 *p,
		     struct nfs_getaclres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_getacl(rqstp, &xdr, res->acl_len);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int
nfs41_xdr_dec_getacl(struct rpc_rqst *rqstp, __be32 *p,
		     struct nfs_getaclres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_getacl(rqstp, &xdr, res->acl_len);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode CLOSE response
 */
static int nfs4_xdr_dec_close(struct xdr_stream *xdr, struct nfs_closeres *res)
{
        int status;

        status = decode_putfh(xdr);
        if (status)
                goto out;
        status = decode_close(xdr, res);
	if (status != 0)
		goto out;
	/*
	 * Note: Server may do delete on close for this file
	 * 	in which case the getattr call will fail with
	 * 	an ESTALE error. Shouldn't be a problem,
	 * 	though, since fattr->valid will remain unset.
	 */
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_close(struct rpc_rqst *rqstp, __be32 *p, struct nfs_closeres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;

	status = nfs4_xdr_dec_close(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_close(struct rpc_rqst *rqstp, __be32 *p,
			       struct nfs_closeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_close(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open(struct xdr_stream *xdr, struct nfs_openres *res)
{
        int status;

        status = decode_putfh(xdr);
        if (status)
                goto out;
        status = decode_savefh(xdr);
	if (status)
		goto out;
        status = decode_open(xdr, res);
        if (status)
                goto out;
	if (decode_getfh(xdr, &res->fh) != 0)
		goto out;
	if (decode_getfattr(xdr, res->f_attr, res->server) != 0)
		goto out;
	if (decode_restorefh(xdr) != 0)
		goto out;
	decode_getfattr(xdr, res->dir_attr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_open(struct rpc_rqst *rqstp, __be32 *p, struct nfs_openres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;

	status = nfs4_xdr_dec_open(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_open(struct rpc_rqst *rqstp, __be32 *p,
			      struct nfs_openres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_open(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode OPEN_CONFIRM response
 */
static int nfs40_xdr_dec_open_confirm(struct rpc_rqst *rqstp, __be32 *p, struct nfs_open_confirmres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_open_confirm(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open_noattr(struct xdr_stream *xdr, struct nfs_openres *res)
{
        int status;

        status = decode_putfh(xdr);
        if (status)
                goto out;
        status = decode_open(xdr, res);
        if (status)
                goto out;
	decode_getfattr(xdr, res->f_attr, res->server);
out:
        return status;
}

static int nfs40_xdr_dec_open_noattr(struct rpc_rqst *rqstp, __be32 *p, struct nfs_openres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;

	status = nfs4_xdr_dec_open_noattr(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_open_noattr(struct rpc_rqst *rqstp, __be32 *p,
				     struct nfs_openres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_open_noattr(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode SETATTR response
 */
static int nfs4_xdr_dec_setattr(struct xdr_stream *xdr, struct nfs_setattrres *res)
{
        int status;

        status = decode_putfh(xdr);
        if (status)
                goto out;
        status = decode_setattr(xdr, res);
        if (status)
                goto out;
	status = decode_getfattr(xdr, res->fattr, res->server);
	if (status == NFS4ERR_DELAY)
		return 0;
out:
	return status;
}

static int nfs40_xdr_dec_setattr(struct rpc_rqst *rqstp, __be32 *p, struct nfs_setattrres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;

	status = nfs4_xdr_dec_setattr(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_setattr(struct rpc_rqst *rqstp, __be32 *p,
				 struct nfs_setattrres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_setattr(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode LOCK response
 */
static int nfs4_xdr_dec_lock(struct xdr_stream *xdr, struct nfs_lock_res *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_lock(xdr, res);
out:
	return status;
}

static int nfs40_xdr_dec_lock(struct rpc_rqst *rqstp, __be32 *p, struct nfs_lock_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_lock(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_lock(struct rpc_rqst *rqstp, __be32 *p,
			      struct nfs_lock_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_lock(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode LOCKT response
 */
static int nfs4_xdr_dec_lockt(struct xdr_stream *xdr, struct nfs_lockt_res *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_lockt(xdr, res);
out:
	return status;
}

static int nfs40_xdr_dec_lockt(struct rpc_rqst *rqstp, __be32 *p, struct nfs_lockt_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_lockt(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_lockt(struct rpc_rqst *rqstp, __be32 *p,
			       struct nfs_lockt_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_lockt(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode LOCKU response
 */
static int nfs4_xdr_dec_locku(struct xdr_stream *xdr, struct nfs_locku_res *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_locku(xdr, res);
out:
	return status;
}

static int nfs40_xdr_dec_locku(struct rpc_rqst *rqstp, __be32 *p, struct nfs_locku_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_locku(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_locku(struct rpc_rqst *rqstp, __be32 *p,
			       struct nfs_locku_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_locku(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode READLINK response
 */
static int nfs4_xdr_dec_readlink(struct rpc_rqst *rqstp, struct xdr_stream *xdr, void *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_readlink(xdr, rqstp);
out:
	return status;
}

static int nfs40_xdr_dec_readlink(struct rpc_rqst *rqstp, __be32 *p, void *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_readlink(rqstp, &xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_readlink(struct rpc_rqst *rqstp, __be32 *p,
				  struct nfs4_readlink_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_readlink(rqstp, &xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode READDIR response
 */
static int nfs4_xdr_dec_readdir(struct rpc_rqst *rqstp, struct xdr_stream *xdr, struct nfs4_readdir_res *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_readdir(xdr, rqstp, res);
out:
	return status;
}

static int nfs40_xdr_dec_readdir(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_readdir_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_readdir(rqstp, &xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_readdir(struct rpc_rqst *rqstp, __be32 *p,
				 struct nfs4_readdir_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_readdir(rqstp, &xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode Read response
 */
static int nfs4_xdr_dec_read(struct rpc_rqst *rqstp, struct xdr_stream *xdr, struct nfs_readres *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_read(xdr, rqstp, res);
	if (!status)
		return res->count;
out:
	return status;
}

static int nfs40_xdr_dec_read(struct rpc_rqst *rqstp, __be32 *p, struct nfs_readres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_read(rqstp, &xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_read(struct rpc_rqst *rqstp, __be32 *p,
			      struct nfs_readres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_read(rqstp, &xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode WRITE response
 */
static int nfs4_xdr_dec_write(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_write(xdr, res);
	if (status)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
	if (!status)
		return res->count;
out:
	return status;
}

static int nfs40_xdr_dec_write(struct rpc_rqst *rqstp, __be32 *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_write(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_write(struct rpc_rqst *rqstp, __be32 *p,
			       struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_write(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode COMMIT response
 */
static int nfs4_xdr_dec_commit(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_commit(xdr, res);
	if (status)
		goto out;
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_commit(struct rpc_rqst *rqstp, __be32 *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;

	status = nfs4_xdr_dec_commit(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_commit(struct rpc_rqst *rqstp, __be32 *p,
				struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_commit(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * FSINFO request
 */
static int nfs4_xdr_dec_fsinfo(struct xdr_stream *xdr, struct nfs_fsinfo *fsinfo)
{
	int status;

	status = decode_putfh(xdr);
	if (!status)
		status = decode_fsinfo(xdr, fsinfo);
	return status;
}

static int nfs40_xdr_dec_fsinfo(struct rpc_rqst *req, __be32 *p,
				struct nfs4_fsinfo_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = nfs4_xdr_dec_fsinfo(&xdr, res->fsinfo);
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_fsinfo(struct rpc_rqst *req, __be32 *p,
				struct nfs4_fsinfo_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res);
	if (!status)
		status = nfs4_xdr_dec_fsinfo(&xdr, res->fsinfo);
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * PATHCONF request
 */
static int nfs4_xdr_dec_pathconf(struct xdr_stream *xdr, struct nfs_pathconf *pathconf)
{
	int status;

	status = decode_putfh(xdr);
	if (!status)
		status = decode_pathconf(xdr, pathconf);
	return status;
}

static int nfs40_xdr_dec_pathconf(struct rpc_rqst *req, __be32 *p,
				  struct nfs4_pathconf_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = nfs4_xdr_dec_pathconf(&xdr, res->pathconf);
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_pathconf(struct rpc_rqst *req, __be32 *p,
				  struct nfs4_pathconf_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res);
	if (!status)
		status = nfs4_xdr_dec_pathconf(&xdr, res->pathconf);
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * STATFS request
 */
static int nfs4_xdr_dec_statfs(struct xdr_stream *xdr, struct nfs_fsstat *fsstat)
{
	int status;

	status = decode_putfh(xdr);
	if (!status)
		status = decode_statfs(xdr, fsstat);
	return status;
}

static int nfs40_xdr_dec_statfs(struct rpc_rqst *req, __be32 *p,
				struct nfs4_statfs_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = nfs4_xdr_dec_statfs(&xdr, res->fsstat);
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_statfs(struct rpc_rqst *req, __be32 *p,
				struct nfs4_statfs_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->seq_res);
	if (!status)
		status = nfs4_xdr_dec_statfs(&xdr, res->fsstat);
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * GETATTR_BITMAP request
 */
static int nfs4_xdr_dec_server_caps(struct xdr_stream *xdr, struct nfs4_server_caps_res *res)
{
	int status;

	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	status = decode_server_caps(xdr, res);
out:
	return status;
}

static int nfs40_xdr_dec_server_caps(struct rpc_rqst *req, __be32 *p, struct nfs4_server_caps_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;

	status = nfs4_xdr_dec_server_caps(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_server_caps(struct rpc_rqst *req, __be32 *p,
				     struct nfs4_server_caps_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_server_caps(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Decode RENEW response
 */
static int nfs40_xdr_dec_renew(struct rpc_rqst *rqstp, __be32 *p, void *dummy)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_renew(&xdr);
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * a SETCLIENTID request
 */
static int nfs40_xdr_dec_setclientid(struct rpc_rqst *req, __be32 *p,
		struct nfs_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_setclientid(&xdr, clp);
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
/*
 * EXCHANGE_ID request
 */
static int nfs41_xdr_dec_exchange_id(struct rpc_rqst *rqstp, uint32_t *p,
				     void *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_exchange_id(&xdr, res);
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * a CREATE_SESSION request
 */
static int nfs41_xdr_dec_create_session(struct rpc_rqst *rqstp, uint32_t *p,
					struct nfs41_create_session_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_create_session(&xdr, res);
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * a DESTROY_SESSION request
 */
static int nfs41_xdr_dec_destroy_session(struct rpc_rqst *rqstp, uint32_t *p,
					 void *dummy)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_destroy_session(&xdr, dummy);
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * a SEQUENCE request
 */
static int nfs41_xdr_dec_sequence(struct rpc_rqst *rqstp, uint32_t *p,
				  struct nfs41_sequence_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, res);
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * a GET_LEASE_TIME request
 */
static int nfs41_xdr_dec_get_lease_time(struct rpc_rqst *rqstp, uint32_t *p,
					struct nfs4_get_lease_time_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_sequence(&xdr, &res->lr_seq_res);
	if (!status)
		status = decode_putrootfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, res->lr_fsinfo);
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * a SETCLIENTID_CONFIRM request
 */
static int nfs40_xdr_dec_setclientid_confirm(struct rpc_rqst *req, __be32 *p, struct nfs_fsinfo *fsinfo)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_setclientid_confirm(&xdr);
	if (!status)
		status = decode_putrootfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, fsinfo);
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * DELEGRETURN request
 */
static int nfs4_xdr_dec_delegreturn(struct xdr_stream *xdr, struct nfs4_delegreturnres *res)
{
	int status;

	status = decode_putfh(xdr);
	if (status != 0)
		goto out;
	status = decode_delegreturn(xdr);
	decode_getfattr(xdr, res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_delegreturn(struct rpc_rqst *rqstp, __be32 *p, struct nfs4_delegreturnres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status != 0)
		goto out;

	status = nfs4_xdr_dec_delegreturn(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_delegreturn(struct rpc_rqst *rqstp, __be32 *p,
				     struct nfs4_delegreturnres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_delegreturn(&xdr, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * FS_LOCATIONS request
 */
static int nfs4_xdr_dec_fs_locations(struct xdr_stream *xdr, struct nfs4_fs_locations *res)
{
	int status;

	if ((status = decode_putfh(xdr)) != 0)
		goto out;
	if ((status = decode_lookup(xdr)) != 0)
		goto out;
	xdr_enter_page(xdr, PAGE_SIZE);
	status = decode_getfattr(xdr, &res->fattr, res->server);
out:
	return status;
}

static int nfs40_xdr_dec_fs_locations(struct rpc_rqst *req, __be32 *p,
				      struct nfs4_fs_locations_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status != 0)
		goto out;

	status = nfs4_xdr_dec_fs_locations(&xdr, res->fs_locations);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_xdr_dec_fs_locations(struct rpc_rqst *req, __be32 *p,
				      struct nfs4_fs_locations_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = nfs4_xdr_dec_fs_locations(&xdr, res->fs_locations);
out:
	return nfs4_fixup_status(status, hdr.status);
}
#endif /* CONFIG_NFS_V4_1 */

#if defined(CONFIG_PNFS)
/*
 * Decode GETDEVICELIST response
 */
static int nfs41_xdr_dec_pnfs_getdevicelist(struct rpc_rqst *rqstp, uint32_t *p,
					struct nfs4_pnfs_getdevicelist_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	dprintk("encoding getdevicelist!\n");

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status != 0)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status != 0)
		goto out;
	status = decode_putfh(&xdr);
	if (status != 0)
		goto out;
	status = decode_getdevicelist(&xdr, res->devlist);
out:
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * Decode GETDEVINFO response
 */
static int nfs41_xdr_dec_pnfs_getdeviceinfo(struct rpc_rqst *rqstp,
					uint32_t *p,
					struct nfs4_pnfs_getdeviceinfo_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status != 0)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status != 0)
		goto out;
	status = decode_getdeviceinfo(&xdr, res->dev);
out:
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * Decode LAYOUTGET response
 */
static int nfs41_xdr_dec_pnfs_layoutget(struct rpc_rqst *rqstp, uint32_t *p,
					struct nfs4_pnfs_layoutget_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_pnfs_layoutget(&xdr, rqstp, res);
out:
	return nfs4_fixup_status(status, hdr.status);

}

/*
 * Decode LAYOUTRETURN response
 */
static int nfs41_xdr_dec_pnfs_layoutreturn(struct rpc_rqst *rqstp, uint32_t *p,
					struct nfs4_pnfs_layoutreturn_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_pnfs_layoutreturn(&xdr, p, res);
out:
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * Decode PNFS WRITE response
 */
static int nfs41_xdr_dec_pnfs_write(struct rpc_rqst *rqstp, uint32_t *p,
				    struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_write(&xdr, res);
	if (!status)
		return res->count;
out:
	return nfs4_fixup_status(status, hdr.status);
}

/*
 * Decode LAYOUTCOMMIT response
 */
static int nfs41_xdr_dec_pnfs_layoutcommit(struct rpc_rqst *rqstp, uint32_t *p,
					   struct pnfs_layoutcommit_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(&xdr, &res->seq_res);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_pnfs_layoutcommit(&xdr, rqstp, res);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server);
out:
	return nfs4_fixup_status(status, hdr.status);
}

#endif /* CONFIG_PNFS */

__be32 *nfs4_decode_dirent(__be32 *p, struct nfs_entry *entry, int plus)
{
	uint32_t bitmap[2] = {0};
	uint32_t len;

	if (!*p++) {
		if (!*p)
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	entry->prev_cookie = entry->cookie;
	p = xdr_decode_hyper(p, &entry->cookie);
	entry->len = ntohl(*p++);
	entry->name = (const char *) p;
	p += XDR_QUADLEN(entry->len);

	/*
	 * In case the server doesn't return an inode number,
	 * we fake one here.  (We don't use inode number 0,
	 * since glibc seems to choke on it...)
	 */
	entry->ino = 1;

	len = ntohl(*p++);		/* bitmap length */
	if (len-- > 0) {
		bitmap[0] = ntohl(*p++);
		if (len-- > 0) {
			bitmap[1] = ntohl(*p++);
			p += len;
		}
	}
	len = XDR_QUADLEN(ntohl(*p++));	/* attribute buffer length */
	if (len > 0) {
		if (bitmap[0] & FATTR4_WORD0_RDATTR_ERROR) {
			bitmap[0] &= ~FATTR4_WORD0_RDATTR_ERROR;
			/* Ignore the return value of rdattr_error for now */
			p++;
			len--;
		}
		if (bitmap[0] == 0 && bitmap[1] == FATTR4_WORD1_MOUNTED_ON_FILEID)
			xdr_decode_hyper(p, &entry->ino);
		else if (bitmap[0] == FATTR4_WORD0_FILEID)
			xdr_decode_hyper(p, &entry->ino);
		p += len;
	}

	entry->eof = !p[0] && p[1];
	return p;
}

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS4_OK,		0		},
	{ NFS4ERR_PERM,		-EPERM		},
	{ NFS4ERR_NOENT,	-ENOENT		},
	{ NFS4ERR_IO,		-errno_NFSERR_IO},
	{ NFS4ERR_NXIO,		-ENXIO		},
	{ NFS4ERR_ACCESS,	-EACCES		},
	{ NFS4ERR_EXIST,	-EEXIST		},
	{ NFS4ERR_XDEV,		-EXDEV		},
	{ NFS4ERR_NOTDIR,	-ENOTDIR	},
	{ NFS4ERR_ISDIR,	-EISDIR		},
	{ NFS4ERR_INVAL,	-EINVAL		},
	{ NFS4ERR_FBIG,		-EFBIG		},
	{ NFS4ERR_NOSPC,	-ENOSPC		},
	{ NFS4ERR_ROFS,		-EROFS		},
	{ NFS4ERR_MLINK,	-EMLINK		},
	{ NFS4ERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{ NFS4ERR_NOTEMPTY,	-ENOTEMPTY	},
	{ NFS4ERR_DQUOT,	-EDQUOT		},
	{ NFS4ERR_STALE,	-ESTALE		},
	{ NFS4ERR_BADHANDLE,	-EBADHANDLE	},
	{ NFS4ERR_BADOWNER,	-EINVAL		},
	{ NFS4ERR_BADNAME,	-EINVAL		},
	{ NFS4ERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFS4ERR_NOTSUPP,	-ENOTSUPP	},
	{ NFS4ERR_TOOSMALL,	-ETOOSMALL	},
	{ NFS4ERR_SERVERFAULT,	-ESERVERFAULT	},
	{ NFS4ERR_BADTYPE,	-EBADTYPE	},
	{ NFS4ERR_LOCKED,	-EAGAIN		},
	{ NFS4ERR_RESOURCE,	-EREMOTEIO	},
	{ NFS4ERR_SYMLINK,	-ELOOP		},
	{ NFS4ERR_OP_ILLEGAL,	-EOPNOTSUPP	},
	{ NFS4ERR_DEADLOCK,	-EDEADLK	},
	{ NFS4ERR_WRONGSEC,	-EPERM		}, /* FIXME: this needs
						    * to be handled by a
						    * middle-layer.
						    */
	{ -1,			-EIO		}
};

/*
 * Convert an NFS error code to a local one.
 * This one is used jointly by NFSv2 and NFSv3.
 */
static int
nfs4_stat_to_errno(int stat)
{
	int i;
	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return nfs_errtbl[i].errno;
	}
	if (stat <= 10000 || stat > 10100) {
		/* The server is looney tunes. */
		return -ESERVERFAULT;
	}
	/* If we cannot translate the error, the recovery routines should
	 * handle it.
	 * Note: remaining NFSv4 error codes have values > 10000, so should
	 * not conflict with native Linux error codes.
	 */
	return -stat;
}

#define PROC(proc, argtype, restype, minorversion)				\
[NFSPROC4_CLNT_##proc] = {					\
	.p_proc   = NFSPROC4_COMPOUND,				\
	.p_encode = (kxdrproc_t) nfs4##minorversion##_xdr_##argtype,	\
	.p_decode = (kxdrproc_t) nfs4##minorversion##_xdr_##restype,	\
	.p_arglen = NFS4##minorversion##_##argtype##_sz,		\
	.p_replen = NFS4##minorversion##_##restype##_sz,		\
	.p_statidx = NFSPROC4_CLNT_##proc,			\
	.p_name   = #proc,					\
    }

struct rpc_procinfo	nfs40_procedures[] = {
  PROC(READ,		enc_read,	dec_read, 0),
  PROC(WRITE,		enc_write,	dec_write, 0),
  PROC(COMMIT,		enc_commit,	dec_commit, 0),
  PROC(OPEN,		enc_open,	dec_open, 0),
  PROC(OPEN_CONFIRM,	enc_open_confirm,	dec_open_confirm, 0),
  PROC(OPEN_NOATTR,	enc_open_noattr,	dec_open_noattr, 0),
  PROC(OPEN_DOWNGRADE,	enc_open_downgrade,	dec_open_downgrade, 0),
  PROC(CLOSE,		enc_close,	dec_close, 0),
  PROC(SETATTR,		enc_setattr,	dec_setattr, 0),
  PROC(FSINFO,		enc_fsinfo,	dec_fsinfo, 0),
  PROC(RENEW,		enc_renew,	dec_renew, 0),
  PROC(SETCLIENTID,	enc_setclientid,	dec_setclientid, 0),
  PROC(SETCLIENTID_CONFIRM,	enc_setclientid_confirm,	dec_setclientid_confirm, 0),
  PROC(LOCK,            enc_lock,       dec_lock, 0),
  PROC(LOCKT,           enc_lockt,      dec_lockt, 0),
  PROC(LOCKU,           enc_locku,      dec_locku, 0),
  PROC(ACCESS,		enc_access,	dec_access, 0),
  PROC(GETATTR,		enc_getattr,	dec_getattr, 0),
  PROC(LOOKUP,		enc_lookup,	dec_lookup, 0),
  PROC(LOOKUP_ROOT,	enc_lookup_root,	dec_lookup_root, 0),
  PROC(REMOVE,		enc_remove,	dec_remove, 0),
  PROC(RENAME,		enc_rename,	dec_rename, 0),
  PROC(LINK,		enc_link,	dec_link, 0),
  PROC(SYMLINK,		enc_symlink,	dec_symlink, 0),
  PROC(CREATE,		enc_create,	dec_create, 0),
  PROC(PATHCONF,	enc_pathconf,	dec_pathconf, 0),
  PROC(STATFS,		enc_statfs,	dec_statfs, 0),
  PROC(READLINK,	enc_readlink,	dec_readlink, 0),
  PROC(READDIR,		enc_readdir,	dec_readdir, 0),
  PROC(SERVER_CAPS,	enc_server_caps, dec_server_caps, 0),
  PROC(DELEGRETURN,	enc_delegreturn, dec_delegreturn, 0),
  PROC(GETACL,		enc_getacl,	dec_getacl, 0),
  PROC(SETACL,		enc_setacl,	dec_setacl, 0),
  PROC(FS_LOCATIONS,	enc_fs_locations, dec_fs_locations, 0),
};

#if defined(CONFIG_NFS_V4_1)
struct rpc_procinfo	nfs41_procedures[] = {
  PROC(READ,		enc_read,	dec_read, 1),
  PROC(WRITE,		enc_write,	dec_write, 1),
  PROC(COMMIT,		enc_commit,	dec_commit, 1),
  PROC(OPEN,		enc_open,	dec_open, 1),
  PROC(OPEN_CONFIRM,	enc_error,	dec_error, 1),
  PROC(OPEN_NOATTR,	enc_open_noattr,	dec_open_noattr, 1),
  PROC(OPEN_DOWNGRADE,	enc_open_downgrade,	dec_open_downgrade, 1),
  PROC(CLOSE,		enc_close,	dec_close, 1),
  PROC(SETATTR,		enc_setattr,	dec_setattr, 1),
  PROC(FSINFO,		enc_fsinfo,	dec_fsinfo, 1),
  PROC(RENEW,		enc_error,	dec_error, 1),
  PROC(SETCLIENTID,	enc_error,	dec_error, 1),
  PROC(SETCLIENTID_CONFIRM, enc_error,	dec_error, 1),
  PROC(LOCK,		enc_lock,	dec_lock, 1),
  PROC(LOCKT,		enc_lockt,	dec_lockt, 1),
  PROC(LOCKU,		enc_locku,	dec_locku, 1),
  PROC(ACCESS,		enc_access,	dec_access, 1),
  PROC(GETATTR,		enc_getattr,	dec_getattr, 1),
  PROC(LOOKUP,		enc_lookup,	dec_lookup, 1),
  PROC(LOOKUP_ROOT,	enc_lookup_root,	dec_lookup_root, 1),
  PROC(REMOVE,		enc_remove,	dec_remove, 1),
  PROC(RENAME,		enc_rename,	dec_rename, 1),
  PROC(LINK,		enc_link,	dec_link, 1),
  PROC(SYMLINK,		enc_symlink,	dec_symlink, 1),
  PROC(CREATE,		enc_create,	dec_create, 1),
  PROC(PATHCONF,	enc_pathconf,	dec_pathconf, 1),
  PROC(STATFS,		enc_statfs,	dec_statfs, 1),
  PROC(READLINK,	enc_readlink,	dec_readlink, 1),
  PROC(READDIR,		enc_readdir,	dec_readdir, 1),
  PROC(SERVER_CAPS,	enc_server_caps, dec_server_caps, 1),
  PROC(DELEGRETURN,	enc_delegreturn, dec_delegreturn, 1),
  PROC(GETACL,		enc_getacl,	dec_getacl, 1),
  PROC(SETACL,		enc_setacl,	dec_setacl, 1),
  PROC(FS_LOCATIONS,	enc_fs_locations, dec_fs_locations, 1),
  PROC(EXCHANGE_ID,	enc_exchange_id,	dec_exchange_id, 1),
  PROC(CREATE_SESSION,	enc_create_session,	dec_create_session, 1),
  PROC(DESTROY_SESSION,	enc_destroy_session,	dec_destroy_session, 1),
  PROC(SEQUENCE,	enc_sequence,	dec_sequence, 1),
  PROC(GET_LEASE_TIME,	enc_get_lease_time,	dec_get_lease_time, 1),
#if defined(CONFIG_PNFS)
  PROC(PNFS_GETDEVICELIST, enc_pnfs_getdevicelist, dec_pnfs_getdevicelist, 1),
  PROC(PNFS_GETDEVICEINFO, enc_pnfs_getdeviceinfo, dec_pnfs_getdeviceinfo, 1),
  PROC(PNFS_LAYOUTGET,	enc_pnfs_layoutget,	dec_pnfs_layoutget, 1),
  PROC(PNFS_LAYOUTCOMMIT, enc_pnfs_layoutcommit,  dec_pnfs_layoutcommit, 1),
  PROC(PNFS_LAYOUTRETURN, enc_pnfs_layoutreturn,  dec_pnfs_layoutreturn, 1),
  PROC(PNFS_WRITE, enc_pnfs_write,  dec_pnfs_write, 1),
#endif /* CONFIG_PNFS */
};
#endif /* CONFIG_NFS_V4_1 */

struct rpc_version      nfs_version4 = {
	.number                 = 4,
};

struct rpc_version      nfs_version40 = {
	.number                 = 4,
	.nrprocs                = ARRAY_SIZE(nfs40_procedures),
	.procs                  = nfs40_procedures
};

#ifdef CONFIG_NFS_V4_1
struct rpc_version      nfs_version41 = {
	.number                 = 4,
	.nrprocs                = ARRAY_SIZE(nfs41_procedures),
	.procs                  = nfs41_procedures
};

#endif /* CONFIG_NFS_V4_1 */

struct rpc_version *nfs4_minorversions[] = {
	&nfs_version40,
#if defined(CONFIG_NFS_V4_1)
	&nfs_version41,
#endif
};

struct rpc_procinfo *nfs4_minorversion_procedures[] = {
	nfs40_procedures,
#if defined(CONFIG_NFS_V4_1)
	nfs41_procedures,
#endif
};

struct rpc_procinfo *nfs4_procedures;

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
