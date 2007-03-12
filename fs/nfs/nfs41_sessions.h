#ifndef __NFS4_1_SESSIONS_H__
#define __NFS4_1_SESSIONS_H__

typedef unsigned char		sessionid_t[16];
typedef u32			streamchannel_attrs;
typedef u32			rdmachannel_attrs;

struct nfs4_channel_attrs {
	unsigned long		max_rqst_sz;
	unsigned long		max_resp_sz;
	unsigned long		max_resp_sz_cached;
	unsigned long		max_ops;
	unsigned long		max_reqs;
	streamchannel_attrs	stream_attrs;
	rdmachannel_attrs	rdma_attrs;
};

struct nfs4_channel {
	struct nfs4_channel_attrs	chan_attrs;
	unsigned long			nr_conns;
	struct list_head		rpc_clients;
};

struct nfs4_session {
	/* Session related params */
	sessionid_t		sess_id;
	u32			seqid;  /* The seqid returned by exchange_id */
	u32			persist;
	u32			header_padding;
	u32			hash_alg;
	u32			ssv_len;
	u32			use_for_back_chan;
	u32			rdma_mode;

	/* Slotid management */
	unsigned long		nr_slots_in_use;
	struct list_head	slots_in_use;
	struct list_head	unused_slots;
	struct rpc_wait_queue	slot_waitq;

	/* The fore and back channel */
	struct nfs4_channel	fore_channel;
	struct nfs4_channel	back_channel;

	unsigned int		expired;
	struct nfs4_client *	client;
	struct list_head	session_hashtbl;
	spinlock_t		session_lock;
	/* To prevent races between create_session and sequence */
	int			mutating;
	struct semaphore	session_sem;
	atomic_t	ref_count;
};

struct nfs4_slot {
	u32			slot_nr;
	u32			seq_nr;
	struct nfs4_session *	session;
	struct list_head	slot_list;
};

#endif
