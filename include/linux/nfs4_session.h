#ifndef _NFS4_SESSIONS_H
#define _NFS4_SESSIONS_H

#if defined(CONFIG_NFS_V4_1)

#include <linux/nfs4.h>
#include <linux/smp_lock.h>
#include <linux/sunrpc/sched.h>

/* The flags for the nfs4_slot struct */
#define NFS4_SLOT_BUSY		0X0	/* Slot in use */
#define NFS4_SLOT_RECLAIMED	0x1	/* Slot has been reclaimed by
					   the server */

struct nfs4_channel_attrs {
	u32			headerpadsz;
	u32			max_rqst_sz;
	u32			max_resp_sz;
	u32			max_resp_sz_cached;
	u32			max_ops;
	u32			max_reqs;
	u32			rdma_attrs;
};

struct nfs4_slot {
	u32		 	slot_nr;
	u32		 	seq_nr;
	unsigned long		flags;
};

struct nfs4_slot_table {
	struct nfs4_slot 	*slots;
	spinlock_t		slot_tbl_lock;
	struct rpc_wait_queue	slot_tbl_waitq;
	int			max_slots;
};

struct nfs4_channel {
	struct nfs4_channel_attrs 	chan_attrs;
	struct rpc_clnt 		*rpc_client;
	struct nfs4_slot_table		slot_table;
};

/*
 * Session related parameters
 */
struct nfs4_session {
	nfs41_sessionid			sess_id;
	u32				flags;
	unsigned long			session_state;
	u32				hash_alg;
	u32				ssv_len;

	/* The fore and back channel */
	struct nfs4_channel		fore_channel;
	struct nfs4_channel		back_channel;

	struct list_head		session_hashtbl;
	spinlock_t 			session_lock;
	atomic_t			ref_count;
	struct rpc_wait_queue		recovery_waitq;
	struct rpc_clnt		       *clnt;
};

#endif	/* CONFIG_NFS_V4_1 */
#endif
