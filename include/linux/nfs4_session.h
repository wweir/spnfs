#ifndef _NFS4_SESSIONS_H
#define _NFS4_SESSIONS_H

#if defined(CONFIG_NFS_V4_1)

#include <linux/nfs4.h>
#include <linux/smp_lock.h>
#include <linux/sunrpc/sched.h>

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
	u32		 	seq_nr;
};

struct nfs4_slot_table {
	struct nfs4_slot 	*slots;
	unsigned long		*used_slots;
	unsigned long		_used_slots;	/* used when max_slots fits */
	spinlock_t		slot_tbl_lock;
	struct rpc_wait_queue	slot_tbl_waitq;
	int			max_slots;
	int			lowest_free_slotid;	/* lower bound hint */
	int			highest_used_slotid;
};

static inline int slot_idx(struct nfs4_slot_table *tbl, struct nfs4_slot *sp)
{
	return sp - tbl->slots;
}

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
	struct rpc_clnt		       *clnt;
};

#endif	/* CONFIG_NFS_V4_1 */
#endif
