#ifndef __NFS4_1_SESSIONS_H__
#define __NFS4_1_SESSIONS_H__

#include <linux/nfs4.h>

/* The flags for the nfs4_slot struct */
#define NFS4_SLOT_BUSY		0X0	/* Slot in use */
#define NFS4_SLOT_RECLAIMED	0x1	/* Slot has been reclaimed by
					   the server */

typedef u32			streamchannel_attrs;
typedef u32			rdmachannel_attrs;

struct nfs4_channel_attrs {
	u32			max_rqst_sz;
	u32			max_resp_sz;
	u32			max_resp_sz_cached;
	u32			max_ops;
	u32			max_reqs;
	rdmachannel_attrs	rdma_attrs;
};

struct nfs4_slot {
	u32		 	slot_nr;
	u32		 	seq_nr;
	unsigned long		flags;
	u32	 		nr_waiters;
	spinlock_t		slot_lock;
};

struct nfs4_slot_table {
	struct nfs4_slot	*slots;
	atomic_t		max_slots;
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
	u32				header_padding;
	u32				hash_alg;
	u32				ssv_len;

	/* The fore and back channel */
	struct nfs4_channel		fore_channel;
	struct nfs4_channel		back_channel;

	unsigned int			expired;
	struct list_head		session_hashtbl;
	spinlock_t 			session_lock;
	atomic_t			ref_count;
};


#endif


