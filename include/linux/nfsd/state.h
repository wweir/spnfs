/*
 *  linux/include/nfsd/state.h
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
 *
 */

#ifndef _NFSD4_STATE_H
#define _NFSD4_STATE_H

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/nfs_xdr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs4.h>

typedef struct {
	u32             cl_boot;
	u32             cl_id;
} clientid_t;

typedef struct {
	u32             so_boot;
	u32             so_stateownerid;
	u32             so_fileid;
} stateid_opaque_t;

typedef struct {
	u32                     si_generation;
	stateid_opaque_t        si_opaque;
} stateid_t;
#define si_boot           si_opaque.so_boot
#define si_stateownerid   si_opaque.so_stateownerid
#define si_fileid         si_opaque.so_fileid


struct nfs4_cb_recall {
	u32			cbr_ident;
	int			cbr_trunc;
	stateid_t		cbr_stateid;
	u32			cbr_fhlen;
	char			cbr_fhval[NFS4_FHSIZE];
	struct nfs4_delegation	*cbr_dp;
};

struct nfs4_delegation {
	struct list_head	dl_perfile;
	struct list_head	dl_perclnt;
	struct list_head	dl_recall_lru;  /* delegation recalled */
	atomic_t		dl_count;       /* ref count */
	struct nfs4_client	*dl_client;
	struct nfs4_file	*dl_file;
	struct file_lock	*dl_flock;
	struct file		*dl_vfs_file;
	u32			dl_type;
	time_t			dl_time;
	struct nfs4_cb_recall	dl_recall;
};

#define dl_stateid      dl_recall.cbr_stateid
#define dl_fhlen        dl_recall.cbr_fhlen
#define dl_fhval        dl_recall.cbr_fhval

/* client delegation callback info */
struct nfs4_callback {
	/* SETCLIENTID info */
	u32                     cb_addr;
	unsigned short          cb_port;
	u32                     cb_prog;
	u32			cb_minorversion;
	u32			cb_ident;	/* minorversion 0 only */
	/* RPC client info */
	atomic_t		cb_set;     /* successful CB_NULL call */
	struct rpc_program      cb_program;
	struct rpc_stat         cb_stat;
	struct rpc_clnt *       cb_client;
};

#if defined(CONFIG_NFSD_V4_1)
/*
 * nfs41_channel
 *
 * for both forward and back channels
 */
struct nfs41_channel {
	u32	ch_headerpad_sz;
	u32	ch_maxreq_sz;
	u32	ch_maxresp_sz;
	u32	ch_maxresp_cached;
	u32	ch_maxops;
	u32	ch_maxreqs;	/* number of slots */
};

/* Maximum number of slots per session - XXX arbitrary */
#define NFS41_MAX_SLOTS 64

/* slot states */
enum {
	NFS4_SLOT_AVAILABLE,
	NFS4_SLOT_INPROGRESS
};

/*
 * nfs41_slot
 *
 * for now, just slot sequence number - will hold DRC for this slot.
 */
struct nfs41_slot {
	atomic_t		sl_state;
	struct nfs41_session	*sl_session;
	u32			sl_seqid;
};

/*
 * nfs41_session
 */
struct nfs41_session {
	struct kref		se_ref;
	struct list_head	se_hash;	/* hash by sessionid_t */
	struct list_head	se_perclnt;
	u32			se_flags;
	struct nfs4_client	*se_client;	/* for expire_client */
	nfs41_sessionid		se_sessionid;
	struct nfs41_channel	se_forward;
	struct nfs41_slot	*se_slots;	/* forward channel slots */
};

#define se_fheaderpad_sz	se_forward.ch_headerpad_sz
#define se_fmaxreq_sz		se_forward.ch_maxreq_sz
#define se_fmaxresp_sz		se_forward.ch_maxresp_sz
#define se_fmaxresp_cached	se_forward.ch_maxresp_cached
#define se_fmaxops		se_forward.ch_maxops
#define se_fnumslots		se_forward.ch_maxreqs

static inline void
nfs41_put_session(struct nfs41_session *ses)
{
	extern void free_session(struct kref *kref);
	kref_put(&ses->se_ref, free_session);
}

static inline void
nfs41_get_session(struct nfs41_session *ses)
{
	kref_get(&ses->se_ref);
}

/* formatted contents of nfs41_sessionid */
struct nfsd_sessionid {
	clientid_t	clientid;
	u32		boot_time;
	u32		sequence;
};

struct current_session {
	struct nfsd_sessionid	cs_sid;
	struct nfs41_slot	*cs_slot;
};

struct nfs41_cb_sequence {
	/* args/res */
	char			cbs_sessionid[NFS4_MAX_SESSIONID_LEN];
	u32			cbs_seqid;
	u32			cbs_slotid;
	u32			cbs_highest_slotid;
	u32			cbsa_cachethis;			/* args only */
	u32			cbsr_target_highest_slotid;	/* res only */
};
#endif /* CONFIG_NFSD_V4_1 */

#define HEXDIR_LEN     33 /* hex version of 16 byte md5 of cl_name plus '\0' */

/*
 * struct nfs4_client - one per client.  Clientids live here.
 * 	o Each nfs4_client is hashed by clientid.
 *
 * 	o Each nfs4_clients is also hashed by name 
 * 	  (the opaque quantity initially sent by the client to identify itself).
 * 	  
 *	o cl_perclient list is used to ensure no dangling stateowner references
 *	  when we expire the nfs4_client
 */
struct nfs4_client {
	struct list_head	cl_idhash; 	/* hash by cl_clientid.id */
	struct list_head	cl_strhash; 	/* hash by cl_name */
	struct list_head	cl_openowners;
	struct list_head	cl_delegations;
#if defined(CONFIG_PNFSD)
	struct list_head	cl_layouts;	/* outstanding layouts */
	struct list_head	cl_layoutrecalls; /* outstanding layoutrecall
						     callbacks */
#endif /* CONFIG_PNFSD */
#if defined(CONFIG_NFSD_V4_1)
	struct list_head	cl_sessions;
#endif /* CONFIG_NFSD_V4_1 */
	struct list_head        cl_lru;         /* tail queue */
	struct xdr_netobj	cl_name; 	/* id generated by client */
	char                    cl_recdir[HEXDIR_LEN]; /* recovery dir */
	nfs4_verifier		cl_verifier; 	/* generated by client */
	time_t                  cl_time;        /* time of last lease renewal */
	__be32			cl_addr; 	/* client ipaddress */
	struct svc_cred		cl_cred; 	/* setclientid principal */
	clientid_t		cl_clientid;	/* generated by server */
	nfs4_verifier		cl_confirm;	/* generated by server */
	struct nfs4_callback	cl_callback;    /* callback info */
	atomic_t		cl_count;	/* ref count */
	u32			cl_firststate;	/* recovery dir creation */
#if defined(CONFIG_NFSD_V4_1)
	u32			cl_seqid;	/* seqid for create_session */
	u32			cl_exchange_flags;
	nfs41_sessionid		cl_sessionid;

	struct svc_xprt		*cl_cb_xprt;	/* 4.1 callback transport */
	struct mutex		cl_cb_mutex;
	/* FIXME: support multiple callback slots */
	u32			cl_cb_seq_nr;
#endif /* CONFIG_NFSD_V4_1 */
};

struct nfs4_fsid {
        u64     major;
        u64     minor;
};

#if defined(CONFIG_PNFSD)

#include <linux/nfsd/nfsd4_pnfs.h>

/* outstanding layout stateid */
struct nfs4_layout_state {
	struct list_head	ls_perfile;
	struct list_head	ls_layouts; /* list of nfs4_layouts */
	struct kref		ls_ref;
	struct nfs4_client	*ls_client;
	struct nfs4_file	*ls_file;
	stateid_t		ls_stateid;
};

/* outstanding layout */
struct nfs4_layout {
	struct list_head		lo_perfile;	/* hash by f_id */
	struct list_head		lo_perclnt;	/* hash by clientid */
	struct list_head		lo_perstate;
	struct nfs4_file		*lo_file;	/* backpointer */
	struct nfs4_client		*lo_client;
	struct nfs4_layout_state	*lo_state;
	struct nfsd4_layout_seg 	lo_seg;
};

/* layoutrecall request (from exported filesystem) */
struct nfs4_layoutrecall {
	struct kref			clr_ref;
	struct nfsd4_pnfs_cb_layout	cb;	/* request */
	struct list_head		clr_perclnt; /* on cl_layoutrecalls */
	struct nfs4_client	       *clr_client;
	struct nfs4_file	       *clr_file;
	int				clr_status;
	struct timespec			clr_time;	/* last activity */
};

/* notify device request (from exported filesystem) */
struct nfs4_notify_device {
	struct nfsd4_pnfs_cb_device	cbd;
	struct nfs4_client	       *cbd_client;
	int				cbd_status;
};

#endif /* CONFIG_PNFSD */

/* struct nfs4_client_reset
 * one per old client. Populates reset_str_hashtbl. Filled from conf_id_hashtbl
 * upon lease reset, or from upcall to state_daemon (to read in state
 * from non-volitile storage) upon reboot.
 */
struct nfs4_client_reclaim {
	struct list_head	cr_strhash;	/* hash by cr_name */
	char			cr_recdir[HEXDIR_LEN]; /* recover dir */
};

static inline void
update_stateid(stateid_t *stateid)
{
	stateid->si_generation++;
}

/* A reasonable value for REPLAY_ISIZE was estimated as follows:  
 * The OPEN response, typically the largest, requires 
 *   4(status) + 8(stateid) + 20(changeinfo) + 4(rflags) +  8(verifier) + 
 *   4(deleg. type) + 8(deleg. stateid) + 4(deleg. recall flag) + 
 *   20(deleg. space limit) + ~32(deleg. ace) = 112 bytes 
 */

#define NFSD4_REPLAY_ISIZE       112 

/*
 * Replay buffer, where the result of the last seqid-mutating operation 
 * is cached. 
 */
struct nfs4_replay {
	__be32			rp_status;
	unsigned int		rp_buflen;
	char			*rp_buf;
	unsigned		intrp_allocated;
	int			rp_openfh_len;
	char			rp_openfh[NFS4_FHSIZE];
	char			rp_ibuf[NFSD4_REPLAY_ISIZE];
};

/*
* nfs4_stateowner can either be an open_owner, or a lock_owner
*
*    so_idhash:  stateid_hashtbl[] for open owner, lockstateid_hashtbl[]
*         for lock_owner
*    so_strhash: ownerstr_hashtbl[] for open_owner, lock_ownerstr_hashtbl[]
*         for lock_owner
*    so_perclient: nfs4_client->cl_perclient entry - used when nfs4_client
*         struct is reaped.
*    so_perfilestate: heads the list of nfs4_stateid (either open or lock) 
*         and is used to ensure no dangling nfs4_stateid references when we 
*         release a stateowner.
*    so_perlockowner: (open) nfs4_stateid->st_perlockowner entry - used when
*         close is called to reap associated byte-range locks
*    so_close_lru: (open) stateowner is placed on this list instead of being
*         reaped (when so_perfilestate is empty) to hold the last close replay.
*         reaped by laundramat thread after lease period.
*/
struct nfs4_stateowner {
	struct kref		so_ref;
	struct list_head        so_idhash;   /* hash by so_id */
	struct list_head        so_strhash;   /* hash by op_name */
	struct list_head        so_perclient;
	struct list_head        so_stateids;
	struct list_head        so_perstateid; /* for lockowners only */
	struct list_head	so_close_lru; /* tail queue */
	time_t			so_time; /* time of placement on so_close_lru */
	int			so_is_open_owner; /* 1=openowner,0=lockowner */
	u32                     so_id;
	struct nfs4_client *    so_client;
	/* after increment in ENCODE_SEQID_OP_TAIL, represents the next
	 * sequence id expected from the client: */
	u32                     so_seqid;
	struct xdr_netobj       so_owner;     /* open owner name */
	int                     so_confirmed; /* successful OPEN_CONFIRM? */
	u32			so_minorversion;
	struct nfs4_replay	so_replay;
};

/*
*  nfs4_file: a file opened by some number of (open) nfs4_stateowners.
*    o fi_perfile list is used to search for conflicting 
*      share_acces, share_deny on the file.
*/
struct nfs4_file {
	struct kref		fi_ref;
	struct list_head        fi_hash;    /* hash by "struct inode *" */
	struct list_head        fi_stateids;
	struct list_head	fi_delegations;
#if defined(CONFIG_PNFSD)
	struct list_head	fi_layouts;
	struct list_head	fi_layout_states;
#endif /* CONFIG_PNFSD */
	struct inode		*fi_inode;
	u32                     fi_id;      /* used with stateowner->so_id 
					     * for stateid_hashtbl hash */
	bool			fi_had_conflict;
#if defined(CONFIG_PNFSD)
	/* used by layoutget / layoutrecall */
	struct nfs4_fsid	fi_fsid;
	u32			fi_fhlen;
	u8			fi_fhval[NFS4_FHSIZE];
#endif /* CONFIG_PNFSD */
};

#if defined(CONFIG_PNFSD)
/* pNFS Metadata server state */

struct pnfs_ds_dev_entry {
	struct list_head	dd_dev_entry; /* st_pnfs_ds_id entry */
	u32			dd_dsid;
};
#endif /* CONFIG_PNFSD */

/*
* nfs4_stateid can either be an open stateid or (eventually) a lock stateid
*
* (open)nfs4_stateid: one per (open)nfs4_stateowner, nfs4_file
*
* 	st_hash: stateid_hashtbl[] entry or lockstateid_hashtbl entry
* 	st_perfile: file_hashtbl[] entry.
* 	st_perfile_state: nfs4_stateowner->so_perfilestate
*       st_perlockowner: (open stateid) list of lock nfs4_stateowners
* 	st_access_bmap: used only for open stateid
* 	st_deny_bmap: used only for open stateid
*	st_openstp: open stateid lock stateid was derived from
*
* XXX: open stateids and lock stateids have diverged sufficiently that
* we should consider defining separate structs for the two cases.
*/

struct nfs4_stateid {
	struct list_head              st_hash; 
	struct list_head              st_perfile;
	struct list_head              st_perstateowner;
	struct list_head              st_lockowners;
#if defined(CONFIG_PNFSD)
	struct list_head              st_pnfs_ds_id;
#endif /* CONFIG_PNFSD */
	struct nfs4_stateowner      * st_stateowner;
	struct nfs4_file            * st_file;
	stateid_t                     st_stateid;
	struct file                 * st_vfs_file;
	unsigned long                 st_access_bmap;
	unsigned long                 st_deny_bmap;
	struct nfs4_stateid         * st_openstp;
};

/* flags for preprocess_seqid_op() */
#define CHECK_FH                0x00000001
#define CONFIRM                 0x00000002
#define OPEN_STATE              0x00000004
#define LOCK_STATE              0x00000008
#define RD_STATE	        0x00000010
#define WR_STATE	        0x00000020
#define CLOSE_STATE             0x00000040
#define DELEG_RET               0x00000080
#define NFS_4_1			0x00000100

#define seqid_mutating_err(err)                       \
	(((err) != nfserr_stale_clientid) &&    \
	((err) != nfserr_bad_seqid) &&          \
	((err) != nfserr_stale_stateid) &&      \
	((err) != nfserr_bad_stateid))

#if defined(CONFIG_NFSD_V4_1)
extern void nfs41_set_slot_state(struct nfs41_slot *, int);
#endif /* CONFIG_NFSD_V4_1 */
extern __be32 nfs4_preprocess_stateid_op(struct svc_fh *current_fh,
		stateid_t *stateid, int flags, struct file **filp);
extern void nfs4_lock_state(void);
extern void nfs4_unlock_state(void);
extern int nfs4_in_grace(void);
extern __be32 nfs4_check_open_reclaim(clientid_t *clid);
extern void put_nfs4_client(struct nfs4_client *clp);
extern void nfs4_free_stateowner(struct kref *kref);
extern void nfsd4_probe_callback(struct nfs4_client *clp);
extern void nfsd4_cb_recall(struct nfs4_delegation *dp);
#if defined(CONFIG_PNFSD)
extern int nfsd4_cb_layout(struct nfs4_layoutrecall *lp);
extern int nfsd4_cb_notify_device(struct nfs4_notify_device *cbnd);
#endif /* CONFIG_PNFSD */
extern void nfs4_put_delegation(struct nfs4_delegation *dp);
extern __be32 nfs4_make_rec_clidname(char *clidname, struct xdr_netobj *clname);
extern void nfsd4_init_recdir(char *recdir_name);
extern int nfsd4_recdir_load(void);
extern void nfsd4_shutdown_recdir(void);
extern int nfs4_client_to_reclaim(const char *name);
extern int nfs4_has_reclaimed_state(const char *name);
extern void nfsd4_recdir_purge_old(void);
extern int nfsd4_create_clid_dir(struct nfs4_client *clp);
extern void nfsd4_remove_clid_dir(struct nfs4_client *clp);
#if defined(CONFIG_PNFSD)
extern int nfs4_preprocess_pnfs_ds_stateid(struct svc_fh *, stateid_t *);
extern struct pnfs_ds_stateid *find_pnfs_ds_stateid(stateid_t *stid);
#endif /* CONFIG_PNFSD */


static inline void
nfs4_put_stateowner(struct nfs4_stateowner *so)
{
	kref_put(&so->so_ref, nfs4_free_stateowner);
}

static inline void
nfs4_get_stateowner(struct nfs4_stateowner *so)
{
	kref_get(&so->so_ref);
}

#endif   /* NFSD4_STATE_H */
