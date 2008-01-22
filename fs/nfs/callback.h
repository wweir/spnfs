/*
 * linux/fs/nfs/callback.h
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback definitions
 */
#ifndef __LINUX_FS_NFS_CALLBACK_H
#define __LINUX_FS_NFS_CALLBACK_H

#define NFS4_CALLBACK 0x40000000
#define NFS4_CALLBACK_XDRSIZE 2048
#define NFS4_CALLBACK_BUFSIZE (1024 + NFS4_CALLBACK_XDRSIZE)

enum nfs4_callback_procnum {
	CB_NULL = 0,
	CB_COMPOUND = 1,
};

enum nfs4_callback_opnum {
	OP_CB_GETATTR = 3,
	OP_CB_RECALL  = 4,
/* Callback operations new to NFSv4.1 */
	OP_CB_LAYOUTRECALL  = 5,
	OP_CB_NOTIFY        = 6,
	OP_CB_PUSH_DELEG    = 7,
	OP_CB_RECALL_ANY    = 8,
	OP_CB_RECALLABLE_OBJ_AVAIL = 9,
	OP_CB_RECALL_SLOT   = 10,
	OP_CB_SEQUENCE      = 11,
	OP_CB_WANTS_CANCELLED = 12,
	OP_CB_NOTIFY_LOCK   = 13,
	OP_CB_ILLEGAL = 10044,
};

struct cb_compound_hdr_arg {
	unsigned int taglen;
	const char *tag;
	unsigned int minorversion;
	unsigned nops;
};

struct cb_compound_hdr_res {
	__be32 *status;
	unsigned int taglen;
	const char *tag;
	__be32 *nops;
};

struct cb_getattrargs {
	struct sockaddr *addr;
	struct nfs_fh fh;
	uint32_t bitmap[2];
};

struct cb_getattrres {
	__be32 status;
	uint32_t bitmap[2];
	uint64_t size;
	uint64_t change_attr;
	struct timespec ctime;
	struct timespec mtime;
};

struct cb_recallargs {
	struct sockaddr *addr;
	struct nfs_fh fh;
	nfs4_stateid stateid;
	uint32_t truncate;
};

#if defined(CONFIG_NFS_V4_1)

struct referring_call {
	uint32_t			rc_sequenceid;
	uint32_t			rc_slotid;
};

struct referring_call_list {
	nfs41_sessionid			rcl_sessionid;
	uint32_t			rcl_nrefcalls;
	struct referring_call 		*rcl_refcalls;
};

struct cb_sequenceargs {
	struct sockaddr_in		*csa_addr;
	nfs41_sessionid			csa_sessionid;
	uint32_t			csa_sequenceid;
	uint32_t			csa_slotid;
	uint32_t			csa_highestslotid;
	uint32_t			csa_cachethis;
	uint32_t			csa_nrclists;
	struct referring_call_list	*csa_rclists;
};

struct cb_sequenceres {
	uint32_t			csr_status;
	nfs41_sessionid			csr_sessionid;
	uint32_t			csr_sequenceid;
	uint32_t			csr_slotid;
	uint32_t			csr_highestslotid;
	uint32_t			csr_target_highestslotid;
};

#endif /* CONFIG_NFS_V4_1 */

extern __be32 nfs4_callback_getattr(struct cb_getattrargs *args, struct cb_getattrres *res);
extern __be32 nfs4_callback_recall(struct cb_recallargs *args, void *dummy);

#ifdef CONFIG_NFS_V4
extern int nfs_callback_up(int minorversion, void *args);
extern void nfs_callback_down(void);
#else
#define nfs_callback_up()	(0)
#define nfs_callback_down()	do {} while(0)
#endif

#ifdef CONFIG_NFS_V4_1
/*
 * Callbacks are expected to not cause substantial latency,
 * so we limit their concurrency to 1.
 */
#define NFS41_BC_MIN_CALLBACKS 1
#define NFS41_BC_MAX_CALLBACKS 1

extern unsigned nfs4_callback_sequence(struct cb_sequenceargs *args,
				       struct cb_sequenceres *res);
#endif /* CONFIG_NFS_V4_1 */

extern unsigned int nfs_callback_set_tcpport;
extern unsigned short nfs_callback_tcpport;

#endif /* __LINUX_FS_NFS_CALLBACK_H */
