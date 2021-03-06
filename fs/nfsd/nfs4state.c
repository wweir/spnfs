
/*
*  linux/fs/nfsd/nfs4state.c
*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
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

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>

#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/mutex.h>
#include <linux/crc32.h>
#include <linux/lockd/bind.h>
#include <linux/module.h>
#if defined(CONFIG_PNFSD)
#include <linux/exportfs.h>
#include <linux/nfsd/pnfsd.h>
#endif /* CONFIG_PNFSD */

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
#if defined(CONFIG_NFSD_V4_1)
static time_t lease_time = 20;     /* default lease time */
static time_t user_lease_time = 20;
#else /* CONFIG_NFSD_V4_1 */
static time_t lease_time = 90;     /* default lease time */
static time_t user_lease_time = 90;
#endif /* CONFIG_NFSD_V4_1 */
static time_t boot_time;
static int in_grace = 1;
static u32 current_ownerid = 1;
static u32 current_fileid = 1;
static u32 current_delegid = 1;
static u32 current_layoutid = 1;
static u32 nfs4_init;
static stateid_t zerostateid;             /* bits all 0 */
static stateid_t onestateid;              /* bits all 1 */
#if defined(CONFIG_NFSD_V4_1)
static u64 current_sessionid = 1;
#endif

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zerostateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &onestateid, sizeof(stateid_t)))

/* forward declarations */
static struct nfs4_stateid * find_stateid(stateid_t *stid, int flags);
static struct nfs4_delegation * find_delegation_stateid(struct inode *ino, stateid_t *stid);
static void release_stateid_lockowners(struct nfs4_stateid *open_stp);
static char user_recovery_dirname[PATH_MAX] = "/var/lib/nfs/v4recovery";
static void nfs4_set_recdir(char *recdir);
#if defined(CONFIG_PNFSD)
/*
 * Layout state - NFSv4.1 pNFS
 */
static struct kmem_cache *pnfs_layout_slab;
static struct kmem_cache *pnfs_layoutrecall_slab;

static int expire_layout(struct nfs4_layout *lp);
static void destroy_layout(struct nfs4_layout *lp);
static void layoutrecall_done(struct nfs4_layoutrecall *clr);
static void release_pnfs_ds_dev_list(struct nfs4_stateid *stp);
#endif /* CONFIG_PNFSD */


/* Locking:
 *
 * client_mutex:
 * 	protects clientid_hashtbl[], clientstr_hashtbl[],
 * 	unconfstr_hashtbl[], uncofid_hashtbl[].
 */
static DEFINE_MUTEX(client_mutex);
static struct thread_info *client_mutex_owner;

static struct kmem_cache *stateowner_slab = NULL;
static struct kmem_cache *file_slab = NULL;
static struct kmem_cache *stateid_slab = NULL;
static struct kmem_cache *deleg_slab = NULL;

#define BUG_ON_UNLOCKED_STATE() BUG_ON(mutex_trylock(&client_mutex) || \
	client_mutex_owner != current_thread_info())

void
nfs4_lock_state(void)
{
	mutex_lock(&client_mutex);
	client_mutex_owner = current_thread_info();
}

void
nfs4_unlock_state(void)
{
	BUG_ON(client_mutex_owner != current_thread_info());
	client_mutex_owner = NULL;
	mutex_unlock(&client_mutex);
}

static int
nfs4_lock_state_nested(void)
{
	if (client_mutex_owner == current_thread_info())
		return 0;
	nfs4_lock_state();
	return 1;
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

/* forward declarations */
static void release_stateowner(struct nfs4_stateowner *sop);
static void release_stateid(struct nfs4_stateid *stp, int flags);

/*
 * Delegation state
 */

/* recall_lock protects the del_recall_lru */
static DEFINE_SPINLOCK(recall_lock);
static struct list_head del_recall_lru;

static void
free_nfs4_file(struct kref *kref)
{
	struct nfs4_file *fp = container_of(kref, struct nfs4_file, fi_ref);
	list_del(&fp->fi_hash);
	iput(fp->fi_inode);
	kmem_cache_free(file_slab, fp);
}

static inline void
put_nfs4_file(struct nfs4_file *fi)
{
	BUG_ON_UNLOCKED_STATE();
	kref_put(&fi->fi_ref, free_nfs4_file);
}

static inline void
get_nfs4_file(struct nfs4_file *fi)
{
	kref_get(&fi->fi_ref);
}

static int num_delegations;
unsigned int max_delegations;

/*
 * Open owner state (share locks)
 */

/* hash tables for nfs4_stateowner */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

#define ownerid_hashval(id) \
        ((id) & OWNER_HASH_MASK)
#define ownerstr_hashval(clientid, ownername) \
        (((clientid) + opaque_hashval((ownername.data), (ownername.len))) & OWNER_HASH_MASK)

static struct list_head	ownerid_hashtbl[OWNER_HASH_SIZE];
static struct list_head	ownerstr_hashtbl[OWNER_HASH_SIZE];

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)
#define FILE_HASH_MASK                  (FILE_HASH_SIZE - 1)
/* hash table for (open)nfs4_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        hash_ptr(x, FILE_HASH_BITS)
#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head stateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct nfs4_stateid *stp, struct svc_fh *current_fh, u32 type)
{
	struct nfs4_delegation *dp;
	struct nfs4_file *fp = stp->st_file;
	struct nfs4_callback *cb = &stp->st_stateowner->so_client->cl_callback;

	dprintk("NFSD alloc_init_deleg\n");
	if (fp->fi_had_conflict)
		return NULL;
	if (num_delegations > max_delegations)
		return NULL;
	dp = kmem_cache_alloc(deleg_slab, GFP_KERNEL);
	if (dp == NULL)
		return dp;
	num_delegations++;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_client = clp;
	get_nfs4_file(fp);
	dp->dl_file = fp;
	dp->dl_flock = NULL;
	get_file(stp->st_vfs_file);
	dp->dl_vfs_file = stp->st_vfs_file;
	dp->dl_type = type;
	dp->dl_recall.cbr_dp = NULL;
	dp->dl_recall.cbr_ident = cb->cb_ident;
	dp->dl_recall.cbr_trunc = 0;
	dp->dl_stateid.si_boot = boot_time;
	dp->dl_stateid.si_stateownerid = current_delegid++;
	dp->dl_stateid.si_fileid = 0;
	dp->dl_stateid.si_generation = 0;
	dp->dl_fhlen = current_fh->fh_handle.fh_size;
	memcpy(dp->dl_fhval, &current_fh->fh_handle.fh_base,
		        current_fh->fh_handle.fh_size);
	dp->dl_time = 0;
	atomic_set(&dp->dl_count, 1);
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	list_add(&dp->dl_perclnt, &clp->cl_delegations);
	return dp;
}

void
nfs4_put_delegation(struct nfs4_delegation *dp)
{
	if (atomic_dec_and_test(&dp->dl_count)) {
		dprintk("NFSD: freeing dp %p\n",dp);
		put_nfs4_file(dp->dl_file);
		kmem_cache_free(deleg_slab, dp);
		num_delegations--;
	}
}

/* Remove the associated file_lock first, then remove the delegation.
 * lease_modify() is called to remove the FS_LEASE file_lock from
 * the i_flock list, eventually calling nfsd's lock_manager
 * fl_release_callback.
 */
static void
nfs4_close_delegation(struct nfs4_delegation *dp)
{
	struct file *filp = dp->dl_vfs_file;

	dprintk("NFSD: close_delegation dp %p\n",dp);
	dp->dl_vfs_file = NULL;
	/* The following nfsd_close may not actually close the file,
	 * but we want to remove the lease in any case. */
	if (dp->dl_flock)
		vfs_setlease(filp, F_UNLCK, &dp->dl_flock);
	BUG_ON_UNLOCKED_STATE();
	nfs4_unlock_state();	/* allow nested layout recall/return */
	nfsd_close(filp);
	nfs4_lock_state();
}

/* Called under the state lock. */
static void
unhash_delegation(struct nfs4_delegation *dp)
{
	list_del_init(&dp->dl_perfile);
	list_del_init(&dp->dl_perclnt);
	spin_lock(&recall_lock);
	list_del_init(&dp->dl_recall_lru);
	spin_unlock(&recall_lock);
	nfs4_close_delegation(dp);
	nfs4_put_delegation(dp);
}

/*
 * SETCLIENTID state
 */

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
	((id) & CLIENT_HASH_MASK)
#define clientstr_hashval(name) \
	(opaque_hashval((name), 8) & CLIENT_HASH_MASK)
/*
 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
 * used in reboot/reset lease grace period processing
 *
 * conf_id_hashtbl[], and conf_str_hashtbl[] hold confirmed
 * setclientid_confirmed info. 
 *
 * unconf_str_hastbl[] and unconf_id_hashtbl[] hold unconfirmed 
 * setclientid info.
 *
 * client_lru holds client queue ordered by nfs4_client.cl_time
 * for lease renewal.
 *
 * close_lru holds (open) stateowner queue ordered by nfs4_stateowner.so_time
 * for last close replay.
 */
static struct list_head	reclaim_str_hashtbl[CLIENT_HASH_SIZE];
static int reclaim_str_hashtbl_size = 0;
static struct list_head	conf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	conf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head client_lru;
static struct list_head close_lru;

#if defined(CONFIG_NFSD_V4_1)
/* Use a prime for hash table size */
#define SESSION_HASH_SIZE	1031
static struct list_head sessionid_hashtbl[SESSION_HASH_SIZE];

int
nfs41_get_slot_state(struct nfs41_slot *slot)
{
	return atomic_read(&slot->sl_state);
}

void
nfs41_set_slot_state(struct nfs41_slot *slot, int state)
{
	atomic_set(&slot->sl_state, state);
}

static int
hash_sessionid(nfs41_sessionid *sessionid)
{
	u32 csum = 0;
	int idx;

	csum = crc32(0, sessionid, sizeof(*sessionid));
	idx = csum % SESSION_HASH_SIZE;
	dprintk("%s IDX: %u csum %u\n", __func__, idx, csum);
	return idx;
}

static inline void
dump_sessionid(const char *fn, nfs41_sessionid *sessionid)
{
	u32 *ptr = (u32 *)(*sessionid);
	dprintk("%s: %u:%u:%u:%u\n", fn, ptr[0], ptr[1], ptr[2], ptr[3]);
}

static void
gen_sessionid(struct nfs41_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	u32 *p = (u32 *)ses->se_sessionid;

	*p++ = clp->cl_clientid.cl_boot;
	*p++ = clp->cl_clientid.cl_id;
	*p++ = (u32)boot_time;
	*p++ = current_sessionid++;
	BUG_ON((char *)p - (char *)ses->se_sessionid !=
	       sizeof(ses->se_sessionid));
}

static int
alloc_init_session(struct nfs4_client *clp, struct nfsd4_create_session *cses)
{
	struct nfs41_session *new;
	int idx, status = nfserr_resource, slotsize, i;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out;

	if (cses->fore_channel.maxreqs >= NFS41_MAX_SLOTS)
		cses->fore_channel.maxreqs = NFS41_MAX_SLOTS;
	new->se_fnumslots = cses->fore_channel.maxreqs;
	slotsize = new->se_fnumslots * sizeof(struct nfs41_slot);

	new->se_slots = kzalloc(slotsize, GFP_KERNEL);
	if (!new->se_slots)
		goto out_free;

	for (i = 0; i < new->se_fnumslots; i++) {
		new->se_slots[i].sl_session = new;
		nfs41_set_slot_state(&new->se_slots[i], NFS4_SLOT_AVAILABLE);
	}

	new->se_client = clp;
	gen_sessionid(new);
	idx = hash_sessionid(&new->se_sessionid);
	memcpy(&clp->cl_sessionid, &new->se_sessionid, sizeof(nfs41_sessionid));

	new->se_flags = cses->flags;

	/* for now, accept the client values */
	new->se_fheaderpad_sz = cses->fore_channel.headerpadsz;
	new->se_fmaxreq_sz = cses->fore_channel.maxreq_sz;
	new->se_fmaxresp_sz = cses->fore_channel.maxresp_sz;
	new->se_fmaxresp_cached = cses->fore_channel.maxresp_cached;
	new->se_fmaxops = cses->fore_channel.maxops;

	kref_init(&new->se_ref);
	INIT_LIST_HEAD(&new->se_hash);
	INIT_LIST_HEAD(&new->se_perclnt);
	list_add(&new->se_hash, &sessionid_hashtbl[idx]);
	list_add(&new->se_perclnt, &clp->cl_sessions);

	status = nfs_ok;
out:
	return status;
out_free:
	kfree(new);
	goto out;
}

struct nfs41_session *
find_in_sessionid_hashtbl(nfs41_sessionid *sessionid)
{
	struct nfs41_session *elem;
	int idx;

	dump_sessionid(__func__, sessionid);
	idx = hash_sessionid(sessionid);
	dprintk("%s: idx is %d\n", __func__, idx);
	/* Search in the appropriate list */
	list_for_each_entry(elem, &sessionid_hashtbl[idx], se_hash) {
		dump_sessionid("list traversal", &elem->se_sessionid);
		if (!memcmp(elem->se_sessionid, sessionid,
			    sizeof(nfs41_sessionid))) {
			dprintk("%s: found session %p\n", __func__, elem);
			return elem;
		}
	}

	dprintk("%s: session not found\n", __func__);
	return NULL;
}

static void
destroy_session(struct nfs41_session *ses)
{
	list_del(&ses->se_hash);
	list_del(&ses->se_perclnt);
	nfs41_put_session(ses);
}

void
free_session(struct kref *kref)
{
	struct nfs41_session *ses;

	ses = container_of(kref, struct nfs41_session, se_ref);
	kfree(ses->se_slots);
	kfree(ses);
}
#endif /* CONFIG_NFSD_V4_1 */

static inline void
renew_client(struct nfs4_client *clp)
{
	/*
	* Move client to the end to the LRU list.
	*/
	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid)
{
	if (clid->cl_boot == boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x) boot_time %08lx\n",
			clid->cl_boot, clid->cl_id, boot_time);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static struct nfs4_client *alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	clp = kzalloc(sizeof(struct nfs4_client), GFP_KERNEL);
	if (clp == NULL)
		return NULL;
	clp->cl_name.data = kmalloc(name.len, GFP_KERNEL);
	if (clp->cl_name.data == NULL) {
		kfree(clp);
		return NULL;
	}
	memcpy(clp->cl_name.data, name.data, name.len);
	clp->cl_name.len = name.len;
	return clp;
}

static void
shutdown_callback_client(struct nfs4_client *clp)
{
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;

	dprintk("NFSD: %s: clp %p cb_client %p\n", __func__, clp, clnt);
	if (clnt) {
		/*
		 * Callback threads take a reference on the client, so there
		 * should be no outstanding callbacks at this point.
		 */
		clp->cl_callback.cb_client = NULL;
		rpc_shutdown_client(clnt);
	}
}

static inline void
free_client(struct nfs4_client *clp)
{
	BUG_ON(!list_empty(&clp->cl_idhash));
	BUG_ON(!list_empty(&clp->cl_strhash));
	BUG_ON(!list_empty(&clp->cl_lru));
	BUG_ON(!list_empty(&clp->cl_delegations));
	BUG_ON(!list_empty(&clp->cl_openowners));
	shutdown_callback_client(clp);
	if (clp->cl_cb_xprt)
		svc_xprt_put(clp->cl_cb_xprt);
	if (clp->cl_cred.cr_group_info)
		put_group_info(clp->cl_cred.cr_group_info);
	kfree(clp->cl_name.data);
	kfree(clp);
}

void
put_nfs4_client(struct nfs4_client *clp)
{
	if (atomic_dec_and_test(&clp->cl_count))
		free_client(clp);
}

static void
expire_client(struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head reaplist;
#if defined(CONFIG_NFSD_V4_1)
	struct nfs41_session  *ses;
#endif /* CONFIG_NFSD_V4_1 */
#if defined(CONFIG_PNFSD)
	struct nfs4_layout *lp;
	struct nfs4_layoutrecall *lrp;
#endif /* CONFIG_PNFSD */

	dprintk("NFSD: expire_client cl_count %d\n",
	                    atomic_read(&clp->cl_count));

	BUG_ON_UNLOCKED_STATE();

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		dprintk("NFSD: expire client. dp %p, fp %p\n", dp,
				dp->dl_flock);
		list_del_init(&dp->dl_perclnt);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	list_del_init(&clp->cl_idhash);
	list_del_init(&clp->cl_strhash);
	list_del_init(&clp->cl_lru);
#if defined(CONFIG_PNFSD)
	while (!list_empty(&clp->cl_layouts)) {
		lp = list_entry(clp->cl_layouts.next, struct nfs4_layout, lo_perclnt);
		dprintk("NFSD: expire client. lp %p, fp %p\n", lp,
				lp->lo_file);
		BUG_ON(lp->lo_client != clp);
		expire_layout(lp);
		destroy_layout(lp);
	}
	while (!list_empty(&clp->cl_layoutrecalls)) {
		lrp = list_entry(clp->cl_layoutrecalls.next,
				struct nfs4_layoutrecall, clr_perclnt);
		dprintk("NFSD: expire client. lrp %p, fp %p\n", lrp,
			lrp->clr_file);
		BUG_ON(lrp->clr_client != clp);
		layoutrecall_done(lrp);
	}
#endif /* CONFIG_PNFSD */
	while (!list_empty(&clp->cl_openowners)) {
		sop = list_entry(clp->cl_openowners.next, struct nfs4_stateowner, so_perclient);
		release_stateowner(sop);
	}
#if defined(CONFIG_NFSD_V4_1)
	while (!list_empty(&clp->cl_sessions)) {
		ses = list_entry(clp->cl_sessions.next, struct nfs41_session,
				 se_perclnt);
		destroy_session(ses);
	}
#endif /* CONFIG_NFSD_V4_1 */
	put_nfs4_client(clp);
}

static struct nfs4_client *create_client(struct xdr_netobj name, char *recdir)
{
	struct nfs4_client *clp;

	clp = alloc_client(name);
	if (clp == NULL)
		return NULL;
	memcpy(clp->cl_recdir, recdir, HEXDIR_LEN);
	atomic_set(&clp->cl_count, 1);
	atomic_set(&clp->cl_callback.cb_set, 0);
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_strhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
#if defined(CONFIG_PNFSD)
	INIT_LIST_HEAD(&clp->cl_layouts);
	INIT_LIST_HEAD(&clp->cl_layoutrecalls);
#endif /* CONFIG_PNFSD */
#if defined(CONFIG_NFSD_V4_1)
	INIT_LIST_HEAD(&clp->cl_sessions);
	mutex_init(&clp->cl_cb_mutex);
#endif /* CONFIG_NFSD_V4_1 */
	INIT_LIST_HEAD(&clp->cl_lru);
	return clp;
}

static void copy_verf(struct nfs4_client *target, nfs4_verifier *source)
{
	memcpy(target->cl_verifier.data, source->data,
			sizeof(target->cl_verifier.data));
}

static void copy_clid(struct nfs4_client *target, struct nfs4_client *source)
{
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static void copy_cred(struct svc_cred *target, struct svc_cred *source)
{
	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
}

static int same_name(const char *n1, const char *n2)
{
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
same_verf(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return 0 == memcmp(v1->data, v2->data, sizeof(v1->data));
}

static int
same_clid(clientid_t *cl1, clientid_t *cl2)
{
	return (cl1->cl_boot == cl2->cl_boot) && (cl1->cl_id == cl2->cl_id);
}

/* XXX what about NGROUP */
static int
same_creds(struct svc_cred *cr1, struct svc_cred *cr2)
{
	return cr1->cr_uid == cr2->cr_uid;
}

static void gen_clid(struct nfs4_client *clp)
{
	static u32 current_clientid = 1;

	clp->cl_clientid.cl_boot = boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void gen_confirm(struct nfs4_client *clp)
{
	static u32 i;
	u32 *p;

	p = (u32 *)clp->cl_confirm.data;
	*p++ = get_seconds();
	*p++ = i++;
}

static int check_name(struct xdr_netobj name)
{
	if (name.len == 0) 
		return 0;
	if (name.len > NFS4_OPAQUE_LIMIT) {
		dprintk("NFSD: check_name: name too long(%d)!\n", name.len);
		return 0;
	}
	return 1;
}

static void
add_to_unconfirmed(struct nfs4_client *clp, unsigned int strhashval)
{
	unsigned int idhashval;

	list_add(&clp->cl_strhash, &unconf_str_hashtbl[strhashval]);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &unconf_id_hashtbl[idhashval]);
	list_add_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	unsigned int strhashval;

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_del_init(&clp->cl_strhash);
	list_move(&clp->cl_idhash, &conf_id_hashtbl[idhashval]);
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_add(&clp->cl_strhash, &conf_str_hashtbl[strhashval]);
	renew_client(clp);
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &conf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &unconf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_confirmed_client_by_str(const char *dname, unsigned int hashval)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &conf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client_by_str(const char *dname, unsigned int hashval)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &unconf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname))
			return clp;
	}
	return NULL;
}

/* a helper function for parse_callback */
static int
parse_octet(unsigned int *lenp, char **addrp)
{
	unsigned int len = *lenp;
	char *p = *addrp;
	int n = -1;
	char c;

	for (;;) {
		if (!len)
			break;
		len--;
		c = *p++;
		if (c == '.')
			break;
		if ((c < '0') || (c > '9')) {
			n = -1;
			break;
		}
		if (n < 0)
			n = 0;
		n = (n * 10) + (c - '0');
		if (n > 255) {
			n = -1;
			break;
		}
	}
	*lenp = len;
	*addrp = p;
	return n;
}

/* parse and set the setclientid ipv4 callback address */
static int
parse_ipv4(unsigned int addr_len, char *addr_val, unsigned int *cbaddrp, unsigned short *cbportp)
{
	int temp = 0;
	u32 cbaddr = 0;
	u16 cbport = 0;
	u32 addrlen = addr_len;
	char *addr = addr_val;
	int i, shift;

	/* ipaddress */
	shift = 24;
	for(i = 4; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbaddr |= (temp << shift);
		if (shift > 0)
		shift -= 8;
	}
	*cbaddrp = cbaddr;

	/* port */
	shift = 8;
	for(i = 2; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbport |= (temp << shift);
		if (shift > 0)
			shift -= 8;
	}
	*cbportp = cbport;
	return 1;
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se)
{
	struct nfs4_callback *cb = &clp->cl_callback;

	/* Currently, we only support tcp for the callback channel */
	if ((se->se_callback_netid_len != 3) || memcmp((char *)se->se_callback_netid_val, "tcp", 3))
		goto out_err;

	if ( !(parse_ipv4(se->se_callback_addr_len, se->se_callback_addr_val,
	                 &cb->cb_addr, &cb->cb_port)))
		goto out_err;

	cb->cb_minorversion = 0;
	cb->cb_prog = se->se_callback_prog;
	cb->cb_ident = se->se_callback_ident;
	return;
out_err:
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

__be32
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_setclientid *setclid)
{
	struct sockaddr_in	*sin = svc_addr_in(rqstp);
	struct xdr_netobj 	clname = { 
		.len = setclid->se_namelen,
		.data = setclid->se_name,
	};
	nfs4_verifier		clverifier = setclid->se_verf;
	unsigned int 		strhashval;
	struct nfs4_client	*conf, *unconf, *new;
	__be32 			status;
	char                    dname[HEXDIR_LEN];
	
	if (!check_name(clname))
		return nfserr_inval;

	status = nfs4_make_rec_clidname(dname, &clname);
	if (status)
		return status;

	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	strhashval = clientstr_hashval(dname);

	nfs4_lock_state();
	conf = find_confirmed_client_by_str(dname, strhashval);
	if (conf) {
		/* RFC 3530 14.2.33 CASE 0: */
		status = nfserr_clid_inuse;
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred)
				|| conf->cl_addr != sin->sin_addr.s_addr) {
			dprintk("NFSD: setclientid: string in use by client"
				"at %u.%u.%u.%u\n", NIPQUAD(conf->cl_addr));
			goto out;
		}
	}
	/*
	 * section 14.2.33 of RFC 3530 (under the heading "IMPLEMENTATION")
	 * has a description of SETCLIENTID request processing consisting
	 * of 5 bullet points, labeled as CASE0 - CASE4 below.
	 */
	unconf = find_unconfirmed_client_by_str(dname, strhashval);
	status = nfserr_resource;
	if (!conf) {
		/*
		 * RFC 3530 14.2.33 CASE 4:
		 * placed first, because it is the normal case
		 */
		if (unconf)
			expire_client(unconf);
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		gen_clid(new);
	} else if (same_verf(&conf->cl_verifier, &clverifier)) {
		/*
		 * RFC 3530 14.2.33 CASE 1:
		 * probable callback update
		 */
		if (unconf) {
			/* Note this is removing unconfirmed {*x***},
			 * which is stronger than RFC recommended {vxc**}.
			 * This has the advantage that there is at most
			 * one {*x***} in either list at any time.
			 */
			expire_client(unconf);
		}
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		copy_clid(new, conf);
	} else if (!unconf) {
		/*
		 * RFC 3530 14.2.33 CASE 2:
		 * probable client reboot; state will be removed if
		 * confirmed.
		 */
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		gen_clid(new);
	} else {
		/*
		 * RFC 3530 14.2.33 CASE 3:
		 * probable client reboot; state will be removed if
		 * confirmed.
		 */
		expire_client(unconf);
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		gen_clid(new);
	}
	copy_verf(new, &clverifier);
	new->cl_addr = sin->sin_addr.s_addr;
	copy_cred(&new->cl_cred, &rqstp->rq_cred);
	gen_confirm(new);
	gen_callback(new, setclid);
	add_to_unconfirmed(new, strhashval);
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

#if defined(CONFIG_NFSD_V4_1)
void nfsd4_setup_callback_channel(void)
{
	return;
}

/*
 * Set the exchange_id flags returned by the server.
 */
static void
nfsd4_set_ex_flags(struct nfs4_client *new, struct nfsd4_exchange_id *clid)
{
#if defined(CONFIG_PNFSD)
	int mds_ds = 0;
#endif /* CONFIG_PNFSD */

	/* Referrals are supported, Migration is not. */
	new->cl_exchange_flags |= EXCHGID4_FLAG_SUPP_MOVED_REFER;

	/* Non pNFS v4.1 is supported */
/* FIXME: bakeathon patch (02-server-nfs4state.patch)
remove EXCHGID4_FLAG_USE_NON_PNFS in cl_exchange flags, even though it is
true that a pnfs server can use NON_PNFS
*/
//???	new->cl_exchange_flags |=  EXCHGID4_FLAG_USE_NON_PNFS;

#if defined(CONFIG_PNFSD)
	/* Save the client's MDS or DS flags, or set them both.
	 * XXX We currently do not have a method of determining
	 * what a server supports prior to receiving a filehandle
	 * e.g. at exchange id time. */

	mds_ds = clid->flags & EXCHGID4_MFS_DS_FLAG_MASK;
	if (mds_ds)
		new->cl_exchange_flags |= mds_ds;
	else
		new->cl_exchange_flags |= EXCHGID4_MFS_DS_FLAG_MASK;
#endif /* CONFIG_PNFSD */

	/* set the wire flags to return to client. */
	clid->flags = new->cl_exchange_flags;
}

__be32 nfsd4_exchange_id(struct svc_rqst *rqstp,
			struct nfsd4_compound_state *cstate,
			struct nfsd4_exchange_id *clid)
{
	struct nfs4_client *unconf, *conf, *new;
	int status;
	unsigned int		strhashval;
	char			dname[HEXDIR_LEN];
	nfs4_verifier		verf = clid->verifier;
	u32			ip_addr = svc_addr_in(rqstp)->sin_addr.s_addr;
	struct xdr_netobj clname = {
		.len = clid->id_len,
		.data = clid->id,
	};

	dprintk("%s rqstp=%p clid=%p clname.len=%u clname.data=%p "
		" ip_addr=%u flags %x\n",
		__func__, rqstp, clid, clname.len, clname.data,
		ip_addr, clid->flags);

	if (!check_name(clname) || (clid->flags & EXCHGID4_INVAL_FLAG_MASK))
		return nfserr_inval;

	status = nfs4_make_rec_clidname(dname, &clname);

	if (status)
		goto error;

	strhashval = clientstr_hashval(dname);

	nfs4_lock_state();
	status = nfserr_clid_inuse;

	conf = find_confirmed_client_by_str(dname, strhashval);
	if (conf) {
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred) ||
		    (ip_addr != conf->cl_addr)) {
			/* Client collision: send nfserr_clid_inuse */
			goto out;
		}

		if (!same_verf(&verf, &conf->cl_verifier)) {
			/* Client reboot: destroy old state */
			expire_client(conf);
			goto out_new;
		}
		/* router replay */
		goto out;
	}

	unconf  = find_unconfirmed_client_by_str(dname, strhashval);
	if (unconf) {
		status = nfs_ok;
		/* Found an unconfirmed record */
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			/* Principal changed: update to the new principal
			 * and send nfs_ok */
			copy_cred(&unconf->cl_cred, &rqstp->rq_cred);
		}

		if (!same_verf(&unconf->cl_verifier, &verf)) {
			/* Reboot before confirmation: update the verifier and
			 * send nfs_ok */
			copy_verf(unconf, &verf);
			new = unconf;
			goto out_copy;
		}
		goto out;
	}

out_new:
	/* Normal case */
	status = nfserr_resource;
	new = create_client(clname, dname);

	if (new == NULL)
		goto out;

	copy_verf(new, &verf);
	copy_cred(&new->cl_cred, &rqstp->rq_cred);
	new->cl_addr = ip_addr;
	gen_clid(new);
	gen_confirm(new);
	add_to_unconfirmed(new, strhashval);

	nfsd4_setup_callback_channel();
out_copy:
	clid->clientid.cl_boot = new->cl_clientid.cl_boot;
	clid->clientid.cl_id = new->cl_clientid.cl_id;

	new->cl_seqid = clid->seqid = 1;
	nfsd4_set_ex_flags(new, clid);

	dprintk("nfsd4_exchange_id seqid %d flags %x\n",
		new->cl_seqid, new->cl_exchange_flags);
	status = nfs_ok;

out:
	nfs4_unlock_state();
error:
	dprintk("nfsd4_exchange_id returns %d\n", ntohl(status));
	return status;
}
#endif /* CONFIG_NFSD_V4_1 */

/*
 * Section 14.2.34 of RFC 3530 (under the heading "IMPLEMENTATION") has
 * a description of SETCLIENTID_CONFIRM request processing consisting of 4
 * bullets, labeled as CASE1 - CASE4 below.
 */
__be32
nfsd4_setclientid_confirm(struct svc_rqst *rqstp,
			 struct nfsd4_compound_state *cstate,
			 struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	struct sockaddr_in *sin = svc_addr_in(rqstp);
	struct nfs4_client *conf, *unconf;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	__be32 status;

	if (STALE_CLIENTID(clid))
		return nfserr_stale_clientid;
	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	nfs4_lock_state();

	conf = find_confirmed_client(clid);
	unconf = find_unconfirmed_client(clid);

	status = nfserr_clid_inuse;
	if (conf && conf->cl_addr != sin->sin_addr.s_addr)
		goto out;
	if (unconf && unconf->cl_addr != sin->sin_addr.s_addr)
		goto out;

	/*
	 * section 14.2.34 of RFC 3530 has a description of
	 * SETCLIENTID_CONFIRM request processing consisting
	 * of 4 bullet points, labeled as CASE1 - CASE4 below.
	 */
	if (conf && unconf && same_verf(&confirm, &unconf->cl_confirm)) {
		/*
		 * RFC 3530 14.2.34 CASE 1:
		 * callback update
		 */
		if (!same_creds(&conf->cl_cred, &unconf->cl_cred))
			status = nfserr_clid_inuse;
		else {
			/* XXX: We just turn off callbacks until we can handle
			  * change request correctly. */
			atomic_set(&conf->cl_callback.cb_set, 0);
			gen_confirm(conf);
			nfsd4_remove_clid_dir(unconf);
			expire_client(unconf);
			status = nfs_ok;

		}
	} else if (conf && !unconf) {
		/*
		 * RFC 3530 14.2.34 CASE 2:
		 * probable retransmitted request; play it safe and
		 * do nothing.
		 */
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred))
			status = nfserr_clid_inuse;
		else
			status = nfs_ok;
	} else if (!conf && unconf
			&& same_verf(&unconf->cl_confirm, &confirm)) {
		/*
		 * RFC 3530 14.2.34 CASE 3:
		 * Normal case; new or rebooted client:
		 */
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			unsigned int hash =
				clientstr_hashval(unconf->cl_recdir);
			conf = find_confirmed_client_by_str(unconf->cl_recdir,
									hash);
			if (conf) {
				nfsd4_remove_clid_dir(conf);
				expire_client(conf);
			}
			move_to_confirmed(unconf);
			conf = unconf;
			nfsd4_probe_callback(conf);
			status = nfs_ok;
		}
	} else if ((!conf || (conf && !same_verf(&conf->cl_confirm, &confirm)))
	    && (!unconf || (unconf && !same_verf(&unconf->cl_confirm,
				    				&confirm)))) {
		/*
		 * RFC 3530 14.2.34 CASE 4:
		 * Client probably hasn't noticed that we rebooted yet.
		 */
		status = nfserr_stale_clientid;
	} else {
		/* check that we have hit one of the cases...*/
		status = nfserr_clid_inuse;
	}
out:
	nfs4_unlock_state();
	return status;
}

#if defined(CONFIG_NFSD_V4_1)
static int
check_slot_seqid(u32 seqid, struct nfs41_slot *slot)
{
	dprintk("%s enter. seqid %d slot->sl_seqid %d\n", __func__, seqid,
		slot->sl_seqid);
	/* Normal */
	if (likely(seqid == slot->sl_seqid + 1))
		return nfs_ok;
	/* Replay */
	if (seqid == slot->sl_seqid)
		return NFSERR_REPLAY_ME;
	/* Wraparound */
	if (seqid == 1 && (slot->sl_seqid + 1) == 0)
		return nfs_ok;
	/* Misordered replay or misordered new request */
	return nfserr_seq_misordered;
}

__be32 nfsd4_create_session(struct svc_rqst *rqstp,
			struct nfsd4_compound_state *cstate,
			struct nfsd4_create_session *session)
{
	u32 ip_addr = svc_addr_in(rqstp)->sin_addr.s_addr;
	struct nfs4_client *conf, *unconf;
	__u32   max_blocksize = svc_max_payload(rqstp);
	int status = 0;

	if (STALE_CLIENTID(&session->clientid))
		return nfserr_stale_clientid;

	nfs4_lock_state();
	unconf = find_unconfirmed_client(&session->clientid);
	conf = find_confirmed_client(&session->clientid);

	if (!conf && !unconf) {
		status = nfserr_stale_clientid;
		goto out;
	}
	if (conf) {
		status = nfs_ok;
		if (conf->cl_seqid == session->seqid) {
			dprintk("Got a create_session replay! seqid= %d\n",
				conf->cl_seqid);
			goto out_replay;
		} else if (session->seqid != conf->cl_seqid + 1) {
			status = nfserr_seq_misordered;
			dprintk("Sequence misordered!\n");
			dprintk("Expected seqid= %d but got seqid= %d\n",
				conf->cl_seqid, session->seqid);
			goto out;
		}
		conf->cl_seqid++;
	} else if (unconf) {
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred) ||
		    (ip_addr != unconf->cl_addr)) {
			status = nfserr_clid_inuse;
			goto out;
		}

		if (unconf->cl_seqid != session->seqid) {
			status = nfserr_seq_misordered;
			goto out;
		}

		move_to_confirmed(unconf);

		/*
		 * We do not support RDMA or persistent sessions
		 */
		session->flags &= ~SESSION4_PERSIST;
		session->flags &= ~SESSION4_RDMA;

		if (!(unconf->cl_exchange_flags & EXCHGID4_FLAG_USE_PNFS_MDS) &&
			unconf->cl_exchange_flags & EXCHGID4_FLAG_USE_PNFS_DS)
			session->flags &= ~SESSION4_BACK_CHAN;

		if (session->flags & SESSION4_BACK_CHAN) {
			unconf->cl_cb_xprt = rqstp->rq_xprt;
			svc_xprt_get(unconf->cl_cb_xprt);
			unconf->cl_callback.cb_minorversion = 1;
			unconf->cl_callback.cb_prog = session->callback_prog;
			nfsd4_probe_callback(unconf);
		}
		conf = unconf;
	}

	status = alloc_init_session(conf, session);

out_replay:
	memcpy(session->sessionid, conf->cl_sessionid, 16);
	session->seqid = conf->cl_seqid;
	session->fore_channel.maxreq_sz = max_blocksize;
	session->fore_channel.maxresp_sz = max_blocksize;
	session->fore_channel.maxresp_cached = max_blocksize;
	session->back_channel.maxreq_sz = max_blocksize;
	session->back_channel.maxresp_sz = max_blocksize;
	session->back_channel.maxresp_cached = max_blocksize;

out:
	nfs4_unlock_state();
	dprintk("%s returns %d\n", __func__, ntohl(status));
	return status;
}
#endif /* CONFIG_NFSD_V4_1 */

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(struct inode *ino, struct svc_fh *current_fh)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(ino);

	fp = kmem_cache_alloc(file_slab, GFP_KERNEL);
	if (fp) {
		kref_init(&fp->fi_ref);
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_stateids);
		INIT_LIST_HEAD(&fp->fi_delegations);
#if defined(CONFIG_PNFSD)
		INIT_LIST_HEAD(&fp->fi_layouts);
		INIT_LIST_HEAD(&fp->fi_layout_states);
#endif /* CONFIG_PNFSD */
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		fp->fi_inode = igrab(ino);
		fp->fi_id = current_fileid++;
		fp->fi_had_conflict = false;
#if defined(CONFIG_PNFSD)
		fp->fi_fsid.major = current_fh->fh_export->ex_fsid;
		fp->fi_fsid.minor = 0;
		fp->fi_fhlen = current_fh->fh_handle.fh_size;
		BUG_ON(fp->fi_fhlen > sizeof(fp->fi_fhval));
		memcpy(fp->fi_fhval, &current_fh->fh_handle.fh_base,
		       fp->fi_fhlen);
#endif /* CONFIG_PNFSD */
		return fp;
	}
	return NULL;
}

static void
nfsd4_free_slab(struct kmem_cache **slab)
{
	if (*slab == NULL)
		return;
	kmem_cache_destroy(*slab);
	*slab = NULL;
}

void
nfsd4_free_slabs(void)
{
	nfsd4_free_slab(&stateowner_slab);
	nfsd4_free_slab(&file_slab);
	nfsd4_free_slab(&stateid_slab);
	nfsd4_free_slab(&deleg_slab);
#if defined(CONFIG_PNFSD)
	nfsd4_free_slab(&pnfs_layout_slab);
	nfsd4_free_slab(&pnfs_layoutrecall_slab);
#endif /* CONFIG_PNFSD */
}

static int
nfsd4_init_slabs(void)
{
	stateowner_slab = kmem_cache_create("nfsd4_stateowners",
			sizeof(struct nfs4_stateowner), 0, 0, NULL);
	if (stateowner_slab == NULL)
		goto out_nomem;
	file_slab = kmem_cache_create("nfsd4_files",
			sizeof(struct nfs4_file), 0, 0, NULL);
	if (file_slab == NULL)
		goto out_nomem;
	stateid_slab = kmem_cache_create("nfsd4_stateids",
			sizeof(struct nfs4_stateid), 0, 0, NULL);
	if (stateid_slab == NULL)
		goto out_nomem;
	deleg_slab = kmem_cache_create("nfsd4_delegations",
			sizeof(struct nfs4_delegation), 0, 0, NULL);
	if (deleg_slab == NULL)
		goto out_nomem;
#if defined(CONFIG_PNFSD)
	pnfs_layout_slab = kmem_cache_create("pnfs_layouts",
			sizeof(struct nfs4_layout), 0, 0, NULL);
	if (pnfs_layout_slab == NULL)
		goto out_nomem;
	pnfs_layoutrecall_slab = kmem_cache_create("pnfs_layoutrecalls",
			sizeof(struct nfs4_layoutrecall), 0, 0, NULL);
	if (pnfs_layoutrecall_slab == NULL)
		goto out_nomem;
#endif /* CONFIG_PNFSD */

	return 0;
out_nomem:
	nfsd4_free_slabs();
	dprintk("nfsd4: out of memory while initializing nfsv4\n");
	return -ENOMEM;
}

void
nfs4_free_stateowner(struct kref *kref)
{
	struct nfs4_stateowner *sop =
		container_of(kref, struct nfs4_stateowner, so_ref);
	kfree(sop->so_owner.data);
	kmem_cache_free(stateowner_slab, sop);
}

static inline struct nfs4_stateowner *
alloc_stateowner(struct xdr_netobj *owner)
{
	struct nfs4_stateowner *sop;

	if ((sop = kmem_cache_alloc(stateowner_slab, GFP_KERNEL))) {
		if ((sop->so_owner.data = kmalloc(owner->len, GFP_KERNEL))) {
			memcpy(sop->so_owner.data, owner->data, owner->len);
			sop->so_owner.len = owner->len;
			kref_init(&sop->so_ref);
			return sop;
		} 
		kmem_cache_free(stateowner_slab, sop);
	}
	return NULL;
}

static struct nfs4_stateowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);  /* not used */
	INIT_LIST_HEAD(&sop->so_close_lru);
	sop->so_time = 0;
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_openowners);
	sop->so_is_open_owner = 1;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
	sop->so_minorversion = open->op_minorversion;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static void
release_stateid_lockowners(struct nfs4_stateid *open_stp)
{
	struct nfs4_stateowner *lock_sop;

	while (!list_empty(&open_stp->st_lockowners)) {
		lock_sop = list_entry(open_stp->st_lockowners.next,
				struct nfs4_stateowner, so_perstateid);
		/* list_del(&open_stp->st_lockowners);  */
		BUG_ON(lock_sop->so_is_open_owner);
		release_stateowner(lock_sop);
	}
}

static void
unhash_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del_init(&sop->so_idhash);
	list_del_init(&sop->so_strhash);
	if (sop->so_is_open_owner)
		list_del_init(&sop->so_perclient);
	list_del_init(&sop->so_perstateid);
	while (!list_empty(&sop->so_stateids)) {
		stp = list_entry(sop->so_stateids.next,
			struct nfs4_stateid, st_perstateowner);
		if (sop->so_is_open_owner)
			release_stateid(stp, OPEN_STATE);
		else
			release_stateid(stp, LOCK_STATE);
	}
}

static void
release_stateowner(struct nfs4_stateowner *sop)
{
	unhash_stateowner(sop);
	list_del_init(&sop->so_close_lru);
	nfs4_put_stateowner(sop);
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop = open->op_stateowner;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners);
	INIT_LIST_HEAD(&stp->st_perfile);
#if defined(CONFIG_PNFSD)
	INIT_LIST_HEAD(&stp->st_pnfs_ds_id);
#endif /* CONFIG_PNFSD */
	list_add(&stp->st_hash, &stateid_hashtbl[hashval]);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	__set_bit(open->op_share_access, &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);
	stp->st_openstp = NULL;
}

static void
release_stateid(struct nfs4_stateid *stp, int flags)
{
	struct file *filp = stp->st_vfs_file;

	list_del(&stp->st_hash);
	list_del(&stp->st_perfile);
	list_del(&stp->st_perstateowner);
#if defined(CONFIG_PNFSD)
	release_pnfs_ds_dev_list(stp);
#endif /* CONFIG_PNFSD */
	if (flags & OPEN_STATE) {
		release_stateid_lockowners(stp);
		stp->st_vfs_file = NULL;
		BUG_ON_UNLOCKED_STATE();
		nfs4_unlock_state();	/* allow nested layout recall/return */
		nfsd_close(filp);
		nfs4_lock_state();
	} else if (flags & LOCK_STATE)
		locks_remove_posix(filp, (fl_owner_t) stp->st_stateowner);
	put_nfs4_file(stp->st_file);
	kmem_cache_free(stateid_slab, stp);
}

static void
move_to_close_lru(struct nfs4_stateowner *sop)
{
	dprintk("NFSD: move_to_close_lru nfs4_stateowner %p\n", sop);

	list_move_tail(&sop->so_close_lru, &close_lru);
	sop->so_time = get_seconds();
}

static int
same_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner,
							clientid_t *clid)
{
	return (sop->so_owner.len == owner->len) &&
		0 == memcmp(sop->so_owner.data, owner->data, owner->len) &&
		(sop->so_client->cl_clientid.cl_id == clid->cl_id);
}

static struct nfs4_stateowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open)
{
	struct nfs4_stateowner *so = NULL;

	BUG_ON_UNLOCKED_STATE();
	list_for_each_entry(so, &ownerstr_hashtbl[hashval], so_strhash) {
		if (same_owner_str(so, &open->op_owner, &open->op_clientid))
			return so;
	}
	return NULL;
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file(struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);
	struct nfs4_file *fp;

	BUG_ON_UNLOCKED_STATE();
	list_for_each_entry(fp, &file_hashtbl[hashval], fi_hash) {
		if (fp->fi_inode == ino) {
			get_nfs4_file(fp);
			return fp;
		}
	}
	return NULL;
}

static struct nfs4_file *
find_alloc_file(struct inode *ino, struct svc_fh *current_fh)
{
	struct nfs4_file *fp;

	fp = find_file(ino);
	if (fp)
		return fp;

	return alloc_init_file(ino, current_fh);
}

#if defined(CONFIG_NFSD_V4_1)
static inline int access_valid(u32 x)
{
	if (x & NFS4_SHARE_INVALID_MASK)
		return 0;
	if ((x & NFS4_SHARE_DENY_MASK) > NFS4_SHARE_ACCESS_BOTH)
		return 0;
	if ((x & NFS4_SHARE_WANT_MASK) > NFS4_SHARE_WANT_CANCEL)
		return 0;
	if ((x & NFS4_SHARE_WHEN_MASK) > NFS4_SHARE_PUSH_DELEG_WHEN_UNCONTENDED)
		return 0;
	return 1;
}
#else  /* CONFIG_NFSD_V4_1 */
static inline int access_valid(u32 x)
{
	if (x < NFS4_SHARE_ACCESS_READ)
		return 0;
	if (x > NFS4_SHARE_ACCESS_BOTH)
		return 0;
	return 1;
}
#endif /* CONFIG_NFSD_V4_1 */

static inline int deny_valid(u32 x)
{
	/* Note: unlike access bits, deny bits may be zero. */
	return x <= NFS4_SHARE_DENY_BOTH;
}

static void
set_access(unsigned int *access, unsigned long bmap) {
	int i;

	*access = 0;
	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			*access |= i;
	}
}

static void
set_deny(unsigned int *deny, unsigned long bmap) {
	int i;

	*deny = 0;
	for (i = 0; i < 4; i++) {
		if (test_bit(i, &bmap))
			*deny |= i ;
	}
}

static int
test_share(struct nfs4_stateid *stp, struct nfsd4_open *open) {
	unsigned int access, deny;

	set_access(&access, stp->st_access_bmap);
	set_deny(&deny, stp->st_deny_bmap);
	if ((access & open->op_share_deny) || (deny & open->op_share_access))
		return 0;
	return 1;
}

/*
 * Called to check deny when READ with all zero stateid or
 * WRITE with all zero or all one stateid
 */
static __be32
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;
	__be32 ret;

	dprintk("NFSD: nfs4_share_conflict\n");

	fp = find_file(ino);
	if (!fp)
		return nfs_ok;
	ret = nfserr_locked;
	/* Search for conflicting share reservations */
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile) {
		if (test_bit(deny_type, &stp->st_deny_bmap) ||
		    test_bit(NFS4_SHARE_DENY_BOTH, &stp->st_deny_bmap))
			goto out;
	}
	ret = nfs_ok;
out:
	put_nfs4_file(fp);
	return ret;
}

static inline void
nfs4_file_downgrade(struct file *filp, unsigned int share_access)
{
	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		put_write_access(filp->f_path.dentry->d_inode);
		filp->f_mode = (filp->f_mode | FMODE_READ) & ~FMODE_WRITE;
	}
}

/*
 * Recall a delegation
 */
static int
do_recall(void *__dp)
{
	struct nfs4_delegation *dp = __dp;

	dp->dl_file->fi_had_conflict = true;
	nfsd4_cb_recall(dp);
	return 0;
}

/*
 * Spawn a thread to perform a recall on the delegation represented
 * by the lease (file_lock)
 *
 * Called from break_lease() with lock_kernel() held.
 * Note: we assume break_lease will only call this *once* for any given
 * lease.
 */
static
void nfsd_break_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp=  (struct nfs4_delegation *)fl->fl_owner;
	struct rpc_clnt *clnt;
	struct task_struct *t;
	int did_lock;

	dprintk("NFSD nfsd_break_deleg_cb: dp %p fl %p\n",dp,fl);
	if (!dp)
		return;

	did_lock = nfs4_lock_state_nested();
	clnt = dp->dl_client->cl_callback.cb_client;
	if (!atomic_read(&dp->dl_client->cl_callback.cb_set) || !clnt) {
		if (did_lock)
			nfs4_unlock_state();
		return;
	}
	kref_get(&clnt->cl_kref);
	if (did_lock)
		nfs4_unlock_state();

	/* We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference: */
	atomic_inc(&dp->dl_count);
	atomic_inc(&dp->dl_client->cl_count);

	spin_lock(&recall_lock);
	list_add_tail(&dp->dl_recall_lru, &del_recall_lru);
	spin_unlock(&recall_lock);

	/* only place dl_time is set. protected by lock_kernel*/
	dp->dl_time = get_seconds();

	/*
	 * We don't want the locks code to timeout the lease for us;
	 * we'll remove it ourself if the delegation isn't returned
	 * in time.
	 */
	fl->fl_break_time = 0;

	t = kthread_run(do_recall, dp, "%s", "nfs4_cb_recall");
	if (IS_ERR(t)) {
		struct nfs4_client *clp = dp->dl_client;

		printk(KERN_INFO "NFSD: Callback thread failed for "
			"for client (clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
		put_nfs4_client(dp->dl_client);
		rpc_release_client(clnt);
		nfs4_lock_state();
		nfs4_put_delegation(dp);
		nfs4_unlock_state();
	}
}

/*
 * The file_lock is being reapd.
 *
 * Called by locks_free_lock() with lock_kernel() held.
 */
static
void nfsd_release_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)fl->fl_owner;

	dprintk("NFSD nfsd_release_deleg_cb: fl %p dp %p dl_count %d\n", fl,dp, atomic_read(&dp->dl_count));

	if (!(fl->fl_flags & FL_LEASE) || !dp)
		return;
	dp->dl_flock = NULL;
}

/*
 * Set the delegation file_lock back pointer.
 *
 * Called from setlease() with lock_kernel() held.
 */
static
void nfsd_copy_lock_deleg_cb(struct file_lock *new, struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)new->fl_owner;

	dprintk("NFSD: nfsd_copy_lock_deleg_cb: new fl %p dp %p\n", new, dp);
	if (!dp)
		return;
	dp->dl_flock = new;
}

/*
 * Called from setlease() with lock_kernel() held
 */
static
int nfsd_same_client_deleg_cb(struct file_lock *onlist, struct file_lock *try)
{
	struct nfs4_delegation *onlistd =
		(struct nfs4_delegation *)onlist->fl_owner;
	struct nfs4_delegation *tryd =
		(struct nfs4_delegation *)try->fl_owner;

	if (onlist->fl_lmops != try->fl_lmops)
		return 0;

	return onlistd->dl_client == tryd->dl_client;
}


static
int nfsd_change_deleg_cb(struct file_lock **onlist, int arg)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg);
	else
		return -EAGAIN;
}

static struct lock_manager_operations nfsd_lease_mng_ops = {
	.fl_break = nfsd_break_deleg_cb,
	.fl_release_private = nfsd_release_deleg_cb,
	.fl_copy_lock = nfsd_copy_lock_deleg_cb,
	.fl_mylease = nfsd_same_client_deleg_cb,
	.fl_change = nfsd_change_deleg_cb,
};


__be32
nfsd4_process_open1(struct nfsd4_open *open)
{
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_stateowner *sop = NULL;

	if (!check_name(open->op_owner))
		return nfserr_inval;

	if (STALE_CLIENTID(&open->op_clientid))
		return nfserr_stale_clientid;

	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	sop = find_openstateowner_str(strhashval, open);
	open->op_stateowner = sop;
	if (!sop) {
		/* Make sure the client's lease hasn't expired. */
		clp = find_confirmed_client(clientid);
		if (clp == NULL)
			return nfserr_expired;
		goto renew;
	}
	if (!sop->so_confirmed) {
		/* Replace unconfirmed owners without checking for replay. */
		clp = sop->so_client;
		release_stateowner(sop);
		open->op_stateowner = NULL;
		goto renew;
	}
	/* Skip seqid processing for NFSv4.1 */
	if (open->op_minorversion == 1)
		goto renew;
	if (open->op_seqid == sop->so_seqid - 1) {
		if (sop->so_replay.rp_buflen)
			return nfserr_replay_me;
		/* The original OPEN failed so spectacularly
		 * that we don't even have replay data saved!
		 * Therefore, we have no choice but to continue
		 * processing this OPEN; presumably, we'll
		 * fail again for the same reason.
		 */
		dprintk("nfsd4_process_open1: replay with no replay cache\n");
		goto renew;
	}
	if (open->op_seqid != sop->so_seqid)
		return nfserr_bad_seqid;
renew:
	if (open->op_stateowner == NULL) {
		sop = alloc_init_open_stateowner(strhashval, clp, open);
		if (sop == NULL)
			return nfserr_resource;
		open->op_stateowner = sop;
	}
	list_del_init(&sop->so_close_lru);
	renew_client(sop->so_client);
	return nfs_ok;
}

static inline __be32
nfs4_check_delegmode(struct nfs4_delegation *dp, int flags)
{
	if ((flags & WR_STATE) && (dp->dl_type == NFS4_OPEN_DELEGATE_READ))
		return nfserr_openmode;
	else
		return nfs_ok;
}

static struct nfs4_delegation *
find_delegation_file(struct nfs4_file *fp, stateid_t *stid)
{
	struct nfs4_delegation *dp;

	list_for_each_entry(dp, &fp->fi_delegations, dl_perfile) {
		if (dp->dl_stateid.si_stateownerid == stid->si_stateownerid)
			return dp;
	}
	return NULL;
}

static __be32
nfs4_check_deleg(struct nfs4_file *fp, struct nfsd4_open *open,
		struct nfs4_delegation **dp)
{
	int flags;
	__be32 status = nfserr_bad_stateid;

	*dp = find_delegation_file(fp, &open->op_delegate_stateid);
	if (*dp == NULL)
		goto out;
	flags = open->op_share_access == NFS4_SHARE_ACCESS_READ ?
						RD_STATE : WR_STATE;
	status = nfs4_check_delegmode(*dp, flags);
	if (status)
		*dp = NULL;
out:
	if (open->op_claim_type != NFS4_OPEN_CLAIM_DELEGATE_CUR)
		return nfs_ok;
	if (status)
		return status;
	open->op_stateowner->so_confirmed = 1;
	return nfs_ok;
}

static __be32
nfs4_check_open(struct nfs4_file *fp, struct nfsd4_open *open, struct nfs4_stateid **stpp)
{
	struct nfs4_stateid *local;
	__be32 status = nfserr_share_denied;
	struct nfs4_stateowner *sop = open->op_stateowner;

	list_for_each_entry(local, &fp->fi_stateids, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		/* remember if we have seen this open owner */
		if (local->st_stateowner == sop)
			*stpp = local;
		/* check for conflicting share reservations */
		if (!test_share(local, open))
			goto out;
	}
	status = 0;
out:
	return status;
}

static inline struct nfs4_stateid *
nfs4_alloc_stateid(void)
{
	return kmem_cache_alloc(stateid_slab, GFP_KERNEL);
}

static __be32
nfs4_new_open(struct svc_rqst *rqstp, struct nfs4_stateid **stpp,
		struct nfs4_delegation *dp,
		struct svc_fh *cur_fh, int flags)
{
	struct nfs4_stateid *stp;

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		return nfserr_resource;

	if (dp) {
		get_file(dp->dl_vfs_file);
		stp->st_vfs_file = dp->dl_vfs_file;
	} else {
		__be32 status;
		status = nfsd_open(rqstp, cur_fh, S_IFREG, flags,
				&stp->st_vfs_file);
		if (status) {
			if (status == nfserr_dropit)
				status = nfserr_jukebox;
			kmem_cache_free(stateid_slab, stp);
			return status;
		}
	}
	*stpp = stp;
	return 0;
}

static inline __be32
nfsd4_truncate(struct svc_rqst *rqstp, struct svc_fh *fh,
		struct nfsd4_open *open)
{
	struct iattr iattr = {
		.ia_valid = ATTR_SIZE,
		.ia_size = 0,
	};
	if (!open->op_truncate)
		return 0;
	if (!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;
	return nfsd_setattr(rqstp, fh, &iattr, 0, (time_t)0);
}

static __be32
nfs4_upgrade_open(struct svc_rqst *rqstp, struct svc_fh *cur_fh, struct nfs4_stateid *stp, struct nfsd4_open *open)
{
	struct file *filp = stp->st_vfs_file;
	struct inode *inode = filp->f_path.dentry->d_inode;
	unsigned int share_access, new_writer;
	__be32 status;

	set_access(&share_access, stp->st_access_bmap);
	new_writer = (~share_access) & open->op_share_access
			& NFS4_SHARE_ACCESS_WRITE;

	if (new_writer) {
		int err = get_write_access(inode);
		if (err)
			return nfserrno(err);
	}
	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status) {
		if (new_writer)
			put_write_access(inode);
		return status;
	}
	/* remember the open */
	filp->f_mode |= open->op_share_access;
	set_bit(open->op_share_access, &stp->st_access_bmap);
	set_bit(open->op_share_deny, &stp->st_deny_bmap);

	return nfs_ok;
}


static void
nfs4_set_claim_prev(struct nfsd4_open *open)
{
	open->op_stateowner->so_confirmed = 1;
	open->op_stateowner->so_client->cl_firststate = 1;
}

/*
 * Attempt to hand out a delegation.
 */
static void
nfs4_open_delegation(struct svc_fh *fh, struct nfsd4_open *open, struct nfs4_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_callback *cb = &sop->so_client->cl_callback;
	struct file_lock fl, *flp = &fl;
	int status, flag = 0;

	flag = NFS4_OPEN_DELEGATE_NONE;
	open->op_recall = 0;
	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_PREVIOUS:
			if (!atomic_read(&cb->cb_set))
				open->op_recall = 1;
			flag = open->op_delegate_type;
			if (flag == NFS4_OPEN_DELEGATE_NONE)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_NULL:
			/* Let's not give out any delegations till everyone's
			 * had the chance to reclaim theirs.... */
			if (nfs4_in_grace())
				goto out;
			if (!atomic_read(&cb->cb_set) || !sop->so_confirmed)
				goto out;
			if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
				flag = NFS4_OPEN_DELEGATE_WRITE;
			else
				flag = NFS4_OPEN_DELEGATE_READ;
			break;
		default:
			goto out;
	}

	dp = alloc_init_deleg(sop->so_client, stp, fh, flag);
	if (dp == NULL) {
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}
	locks_init_lock(&fl);
	fl.fl_lmops = &nfsd_lease_mng_ops;
	fl.fl_flags = FL_LEASE;
	fl.fl_end = OFFSET_MAX;
	fl.fl_owner =  (fl_owner_t)dp;
	fl.fl_file = stp->st_vfs_file;
	fl.fl_pid = current->tgid;

	/* vfs_setlease checks to see if delegation should be handed out.
	 * the lock_manager callbacks fl_mylease and fl_change are used
	 */
	if ((status = vfs_setlease(stp->st_vfs_file,
		flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK, &flp))) {
		dprintk("NFSD: setlease failed [%d], no delegation\n", status);
		unhash_delegation(dp);
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}

	memcpy(&open->op_delegate_stateid, &dp->dl_stateid, sizeof(dp->dl_stateid));

	dprintk("NFSD: delegation stateid=(%08x/%08x/%08x/%08x)\n\n",
	             dp->dl_stateid.si_boot,
	             dp->dl_stateid.si_stateownerid,
	             dp->dl_stateid.si_fileid,
	             dp->dl_stateid.si_generation);
out:
	if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS
			&& flag == NFS4_OPEN_DELEGATE_NONE
			&& open->op_delegate_type != NFS4_OPEN_DELEGATE_NONE)
		dprintk("NFSD: WARNING: refusing delegation reclaim\n");
	open->op_delegate_type = flag;
}

/*
 * called with nfs4_lock_state() held.
 */
__be32
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfs4_file *fp = NULL;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	__be32 status;

	status = nfserr_inval;
	if (!access_valid(open->op_share_access)
			|| !deny_valid(open->op_share_deny))
		goto out;
	/*
	 * Lookup file; if found, lookup stateid and check open request,
	 * and check for delegations in the process of being recalled.
	 * If not found, create the nfs4_file struct
	 */
	fp = find_file(ino);
	if (fp) {
		if ((status = nfs4_check_open(fp, open, &stp)))
			goto out;
		status = nfs4_check_deleg(fp, open, &dp);
		if (status)
			goto out;
	} else {
		status = nfserr_bad_stateid;
		if (open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR)
			goto out;
		status = nfserr_resource;
		fp = alloc_init_file(ino, current_fh);
		if (fp == NULL)
			goto out;
	}

	/*
	 * OPEN the file, or upgrade an existing OPEN.
	 * If truncate fails, the OPEN fails.
	 */
	if (stp) {
		/* Stateid was found, this is an OPEN upgrade */
		status = nfs4_upgrade_open(rqstp, current_fh, stp, open);
		if (status)
			goto out;
		update_stateid(&stp->st_stateid);
	} else {
		/* Stateid was not found, this is a new OPEN */
		int flags = 0;
		if (open->op_share_access & NFS4_SHARE_ACCESS_READ)
			flags |= MAY_READ;
		if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
			flags |= MAY_WRITE;
		status = nfs4_new_open(rqstp, &stp, dp, current_fh, flags);
		if (status)
			goto out;
		init_stateid(stp, fp, open);
		status = nfsd4_truncate(rqstp, current_fh, open);
		if (status) {
			release_stateid(stp, OPEN_STATE);
			goto out;
		}
		if (open->op_minorversion == 1)
			update_stateid(&stp->st_stateid);
	}
	memcpy(&open->op_stateid, &stp->st_stateid, sizeof(stateid_t));

	/*
	* Attempt to hand out a delegation. No error return, because the
	* OPEN succeeds even if we fail.
	*/
	nfs4_open_delegation(current_fh, open, stp);

	status = nfs_ok;
	if (open->op_minorversion == 1)
		open->op_stateowner->so_confirmed = 1;

	dprintk("nfs4_process_open2: stateid=(%08x/%08x/%08x/%08x)\n",
	            stp->st_stateid.si_boot, stp->st_stateid.si_stateownerid,
	            stp->st_stateid.si_fileid, stp->st_stateid.si_generation);
out:
	if (fp)
		put_nfs4_file(fp);
	if (status == 0 && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		nfs4_set_claim_prev(open);
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = NFS4_OPEN_RESULT_LOCKTYPE_POSIX;
	if (!open->op_stateowner->so_confirmed && !open->op_minorversion)
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	return status;
}

static struct workqueue_struct *laundry_wq;
static void laundromat_main(struct work_struct *);
static DECLARE_DELAYED_WORK(laundromat_work, laundromat_main);

__be32
nfsd4_renew(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    clientid_t *clid)
{
	struct nfs4_client *clp;
	__be32 status;

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	clp = find_confirmed_client(clid);
	status = nfserr_expired;
	if (clp == NULL) {
		/* We assume the client took too long to RENEW. */
		dprintk("nfsd4_renew: clientid not found!\n");
		goto out;
	}
	renew_client(clp);
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_delegations)
			&& !atomic_read(&clp->cl_callback.cb_set))
		goto out;
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

#if defined(CONFIG_NFSD_V4_1)
__be32
nfsd4_sequence(struct svc_rqst *r,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_sequence *seq)
{
	struct nfs41_session *elem;
	struct nfs41_slot *slot;
	struct current_session *c_ses = cstate->current_ses;
	int status;

	if (STALE_CLIENTID((clientid_t *)seq->sessionid))
		return nfserr_stale_clientid;

	nfs4_lock_state();
	status = nfserr_badsession;
	elem = find_in_sessionid_hashtbl(&seq->sessionid);
	if (!elem)
		goto out;

	status = nfserr_badslot;
	if (seq->slotid >= elem->se_fnumslots)
		goto out;

	slot = &elem->se_slots[seq->slotid];
	dprintk("%s: slotid %d\n", __func__, seq->slotid);

	/* Server post op_sequence compound processing had an upcall which
	 * resulted in replaying the compound processing including the
	 * already processed op_sequence. Set current_session
	 * but don't bump slot->sl_seqid which was incremented in successful
	 * op_sequence processing prior to upcall.
	 */
	if (nfs41_get_slot_state(slot) == NFS4_SLOT_INPROGRESS) {
		dprintk("%s: NFS4_SLOT_INPROGRESS. set current_session\n",
			__func__);
		goto set_curr_ses;
	}

	status = check_slot_seqid(seq->seqid, slot);
	if (status == NFSERR_REPLAY_ME)
		goto replay;
	else if (status)
		goto out;

	/* Success! bump slot seqid and renew clientid */
	slot->sl_seqid = seq->seqid;
	renew_client(elem->se_client);
	dprintk("%s: set NFS4_SLOT_INPROGRESS\n", __func__);
	nfs41_set_slot_state(slot, NFS4_SLOT_INPROGRESS);

set_curr_ses:
	/* Set current_session. hold reference until done processing compound.
	 * nfs41_put_session called only if cs_slot is set
	 */
	memcpy(&c_ses->cs_sid, &seq->sessionid, sizeof(c_ses->cs_sid));
	BUG_ON(sizeof(c_ses->cs_sid) != sizeof(seq->sessionid));
	c_ses->cs_slot = slot;
	nfs41_get_session(slot->sl_session);

	/* FIXME: for now just initialize target_highest_slotid and flags
	 * response fields */
	seq->target_maxslots = seq->maxslots;
	seq->status_flags = 0;

	status = nfs_ok;
out:
	dprintk("%s: return %d\n", __func__, ntohl(status));
	nfs4_unlock_state();
	return status;
replay:
	dprintk("%s: REPLAY - AKKKK! no code yet! return BAD SESSION\n",
		__func__);
	status = nfserr_badsession;
	goto out;
}

__be32
nfsd4_destroy_session(struct svc_rqst *r,
			struct nfsd4_compound_state *cstate,
			struct nfsd4_destroy_session *sessionid)
{
	struct nfs41_session *ses;
	u32 status = nfserr_badsession;

	/* Notes:
	 * - The confirmed nfs4_client->cl_sessionid holds destroyed sessinid
	 * - Should we return nfserr_back_chan_busy if waiting for
	 *   callbacks on to-be-destroyed session?
	 * - Do we need to clear any callback info from previous session?
	 */

	dump_sessionid(__func__, &sessionid->sessionid);
	nfs4_lock_state();
	ses = find_in_sessionid_hashtbl(&sessionid->sessionid);
	if (!ses)
		goto out;

	/* wait for callbacks */
	shutdown_callback_client(ses->se_client);

	destroy_session(ses);
	status = nfs_ok;
out:
	nfs4_unlock_state();
	dprintk("%s returns %d\n", __func__, ntohl(status));
	return status;
}
#endif /* CONFIG_NFSD_V4_1 */

static void
end_grace(void)
{
	dprintk("NFSD: end of grace period\n");
	nfsd4_recdir_purge_old();
	in_grace = 0;
}

static time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, clientid_val = NFSD_LEASE_TIME;
	time_t u, test_val = NFSD_LEASE_TIME;

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting\n");
	if (in_grace)
		end_grace();
	list_for_each_safe(pos, next, &client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (clientid_val > t)
				clientid_val = t;
			break;
		}
#if defined(CONFIG_PNFSD)
		if (clp->cl_exchange_flags & EXCHGID4_FLAG_USE_PNFS_DS)
			break;
#endif /* CONFIG_PNFSD */
		dprintk("NFSD: purging unused client(clientid %08x flags %x)\n",
			clp->cl_clientid.cl_id, clp->cl_exchange_flags);
		nfsd4_remove_clid_dir(clp);
		expire_client(clp);
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			u = dp->dl_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused delegation dp %p, fp %p\n",
			            dp, dp->dl_flock);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	test_val = NFSD_LEASE_TIME;
	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		if (time_after((unsigned long)sop->so_time, (unsigned long)cutoff)) {
			u = sop->so_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused open stateowner (so_id %d)\n",
			sop->so_id);
		release_stateowner(sop);
	}
	if (clientid_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		clientid_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return clientid_val;
}

void
laundromat_main(struct work_struct *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	queue_delayed_work(laundry_wq, &laundromat_work, t*HZ);
}

static struct nfs4_stateowner *
search_close_lru(u32 st_id, int flags)
{
	struct nfs4_stateowner *local = NULL;

	if (flags & CLOSE_STATE) {
		list_for_each_entry(local, &close_lru, so_close_lru) {
			if (local->so_id == st_id)
				return local;
		}
	}
	return NULL;
}

static inline int
nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stateid *stp)
{
	return fhp->fh_dentry->d_inode != stp->st_vfs_file->f_path.dentry->d_inode;
}

static int
STALE_STATEID(stateid_t *stateid)
{
	if (stateid->si_boot == boot_time)
		return 0;
	dprintk("NFSD: stale stateid (%08x/%08x/%08x/%08x)!\n",
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
	return 1;
}

static inline int
access_permit_read(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_READ, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap);
}

static inline int
access_permit_write(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap);
}

static
__be32 nfs4_check_openmode(struct nfs4_stateid *stp, int flags)
{
        __be32 status = nfserr_openmode;

	if ((flags & WR_STATE) && (!access_permit_write(stp->st_access_bmap)))
                goto out;
	if ((flags & RD_STATE) && (!access_permit_read(stp->st_access_bmap)))
                goto out;
	status = nfs_ok;
out:
	return status;
}

static inline __be32
check_special_stateids(svc_fh *current_fh, stateid_t *stateid, int flags)
{
	/* Trying to call delegreturn with a special stateid? Yuch: */
	if (!(flags & (RD_STATE | WR_STATE)))
		return nfserr_bad_stateid;
	else if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (nfs4_in_grace()) {
		/* Answer in remaining cases depends on existance of
		 * conflicting state; so we must wait out the grace period. */
		return nfserr_grace;
	} else if (flags & WR_STATE)
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_WRITE);
	else /* (flags & RD_STATE) && ZERO_STATEID(stateid) */
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_READ);
}

/*
 * Allow READ/WRITE during grace period on recovered state only for files
 * that are not able to provide mandatory locking.
 */
static inline int
io_during_grace_disallowed(struct inode *inode, int flags)
{
	return nfs4_in_grace() && (flags & (RD_STATE | WR_STATE))
		&& mandatory_lock(inode);
}

/*
* Checks for stateid operations
*/
__be32
nfs4_preprocess_stateid_op(struct svc_fh *current_fh, stateid_t *stateid, int flags, struct file **filpp)
{
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	stateid_t *stidp;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	__be32 status;

	dprintk("NFSD: preprocess_stateid_op: stateid = (%08x/%08x/%08x/%08x)\n",
		stateid->si_boot, stateid->si_stateownerid,
		stateid->si_fileid, stateid->si_generation);
	if (filpp)
		*filpp = NULL;

	if (io_during_grace_disallowed(ino, flags))
		return nfserr_grace;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return check_special_stateids(current_fh, stateid, flags);

#if defined(CONFIG_PNFSD)
	if (pnfs_fh_is_ds(&current_fh->fh_handle)) {
		/* PNFS FH */
		status = nfs4_preprocess_pnfs_ds_stateid(current_fh, stateid);
		goto out;
	}
#endif /* CONFIG_PNFSD */

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (!stateid->si_fileid) { /* delegation stateid */
		if(!(dp = find_delegation_stateid(ino, stateid))) {
			dprintk("NFSD: delegation stateid not found\n");
			goto out;
		}
		stidp = &dp->dl_stateid;
	} else { /* open or lock stateid */
		if (!(stp = find_stateid(stateid, flags))) {
			dprintk("NFSD: open or lock stateid not found\n");
			goto out;
		}
		if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp))
			goto out;
		if (!stp->st_stateowner->so_confirmed)
			goto out;
		stidp = &stp->st_stateid;
	}
	/*
	* 4.1 is allowed to ignore the generation number when it is zero
	* whereas 4.0 returns bad_stateid or stale stateid.
	*/
	if ((flags & NFS_4_1) && stateid->si_generation == 0)
		goto checkmode;

	if (stateid->si_generation > stidp->si_generation)
		goto out;

	/* OLD STATEID */
	status = nfserr_old_stateid;
	if (stateid->si_generation < stidp->si_generation)
		goto out;

checkmode:
	if (stp) {
		if ((status = nfs4_check_openmode(stp,flags)))
			goto out;
		renew_client(stp->st_stateowner->so_client);
		if (filpp)
			*filpp = stp->st_vfs_file;
	} else if (dp) {
		if ((status = nfs4_check_delegmode(dp, flags)))
			goto out;
		renew_client(dp->dl_client);
		if (flags & DELEG_RET)
			unhash_delegation(dp);
		if (filpp)
			*filpp = dp->dl_vfs_file;
	}
	status = nfs_ok;
out:
	return status;
}

static inline int
setlkflg (int type)
{
	return (type == NFS4_READW_LT || type == NFS4_READ_LT) ?
		RD_STATE : WR_STATE;
}

/* 
 * Checks for sequence id mutating operations. 
 */
static __be32
nfs4_preprocess_seqid_op(struct svc_fh *current_fh, u32 seqid, stateid_t *stateid, int flags, struct nfs4_stateowner **sopp, struct nfs4_stateid **stpp, struct nfsd4_lock *lock)
{
	struct nfs4_stateid *stp;
	struct nfs4_stateowner *sop;

	dprintk("NFSD: preprocess_seqid_op: seqid=%d " 
			"stateid = (%08x/%08x/%08x/%08x)\n", seqid,
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);

	*stpp = NULL;
	*sopp = NULL;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		dprintk("NFSD: preprocess_seqid_op: magic stateid!\n");
		return nfserr_bad_stateid;
	}

	if (STALE_STATEID(stateid))
		return nfserr_stale_stateid;
	/*
	* We return BAD_STATEID if filehandle doesn't match stateid, 
	* the confirmed flag is incorrecly set, or the generation 
	* number is incorrect.  
	*/
	stp = find_stateid(stateid, flags);
	if (stp == NULL) {
		/*
		 * Also, we should make sure this isn't just the result of
		 * a replayed close:
		 */
		sop = search_close_lru(stateid->si_stateownerid, flags);
		if (sop == NULL)
			return nfserr_bad_stateid;
		*sopp = sop;
		goto check_replay;
	}

	*stpp = stp;
	*sopp = sop = stp->st_stateowner;

	if (lock) {
		clientid_t *lockclid = &lock->v.new.clientid;
		struct nfs4_client *clp = sop->so_client;
		int lkflg = 0;
		__be32 status;

		lkflg = setlkflg(lock->lk_type);

		if (lock->lk_is_new) {
			if (!sop->so_is_open_owner)
				return nfserr_bad_stateid;
			if (sop->so_minorversion == 0 &&
			    !same_clid(&clp->cl_clientid, lockclid))
			       return nfserr_bad_stateid;
			/* stp is the open stateid */
			status = nfs4_check_openmode(stp, lkflg);
			if (status)
				return status;
		} else {
			/* stp is the lock stateid */
			status = nfs4_check_openmode(stp->st_openstp, lkflg);
			if (status)
				return status;
               }
	}

	if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp)) {
		dprintk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		return nfserr_bad_stateid;
	}

	/*
	*  We now validate the seqid and stateid generation numbers.
	*  For the moment, we ignore the possibility of 
	*  generation number wraparound.
	*/
	if (sop->so_minorversion == 0 && seqid != sop->so_seqid)
		goto check_replay;

	if (sop->so_confirmed && flags & CONFIRM) {
		dprintk("NFSD: preprocess_seqid_op: expected"
				" unconfirmed stateowner!\n");
		return nfserr_bad_stateid;
	}
	if (!sop->so_confirmed && !(flags & CONFIRM)) {
		dprintk("NFSD: preprocess_seqid_op: stateowner not"
				" confirmed yet!\n");
		return nfserr_bad_stateid;
	}

	/*
	* 4.1 is allowed to ignore the generation number when it is zero
	*/
	if (sop->so_minorversion == 1 && stateid->si_generation == 0)
			goto renew; /* skip v4.0 generation number checks */

	if (stateid->si_generation > stp->st_stateid.si_generation) {
		dprintk("NFSD: preprocess_seqid_op: future stateid?!\n");
		return nfserr_bad_stateid;
	}

	if (stateid->si_generation < stp->st_stateid.si_generation) {
		dprintk("NFSD: preprocess_seqid_op: old stateid!\n");
		return nfserr_old_stateid;
	}
renew:
	renew_client(sop->so_client);
	return nfs_ok;

check_replay:
	if (seqid == sop->so_seqid - 1) {
		dprintk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* indicate replay to calling function */
		return nfserr_replay_me;
	}
	dprintk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d)\n",
			sop->so_seqid, seqid);
	*sopp = NULL;
	return nfserr_bad_seqid;
}

__be32
nfsd4_open_confirm(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_open_confirm *oc)
{
	__be32 status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0);
	if (status)
		return status;

	nfs4_lock_state();

	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					oc->oc_seqid, &oc->oc_req_stateid,
					CHECK_FH | CONFIRM | OPEN_STATE,
					&oc->oc_stateowner, &stp, NULL)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);

	nfsd4_create_clid_dir(sop->so_client);
out:
	if (oc->oc_stateowner) {
		nfs4_get_stateowner(oc->oc_stateowner);
		cstate->replay_owner = oc->oc_stateowner;
	}
	nfs4_unlock_state();
	return status;
}


/*
 * unset all bits in union bitmap (bmap) that
 * do not exist in share (from successful OPEN_DOWNGRADE)
 */
static void
reset_union_bmap_access(unsigned long access, unsigned long *bmap)
{
	int i;
	for (i = 1; i < 4; i++) {
		if ((i & access) != i)
			__clear_bit(i, bmap);
	}
}

static void
reset_union_bmap_deny(unsigned long deny, unsigned long *bmap)
{
	int i;
	for (i = 0; i < 4; i++) {
		if ((i & deny) != i)
			__clear_bit(i, bmap);
	}
}

__be32
nfsd4_open_downgrade(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_open_downgrade *od)
{
	__be32 status;
	struct nfs4_stateid *stp;
	unsigned int share_access;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	if (!access_valid(od->od_share_access)
			|| !deny_valid(od->od_share_deny))
		return nfserr_inval;

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					od->od_seqid,
					&od->od_stateid, 
					CHECK_FH | OPEN_STATE, 
					&od->od_stateowner, &stp, NULL)))
		goto out; 

	status = nfserr_inval;
	if (!test_bit(od->od_share_access, &stp->st_access_bmap)) {
		dprintk("NFSD:access not a subset current bitmap: 0x%lx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto out;
	}
	if (!test_bit(od->od_share_deny, &stp->st_deny_bmap)) {
		dprintk("NFSD:deny not a subset current bitmap: 0x%lx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto out;
	}
	set_access(&share_access, stp->st_access_bmap);
	nfs4_file_downgrade(stp->st_vfs_file,
	                    share_access & ~od->od_share_access);

	reset_union_bmap_access(od->od_share_access, &stp->st_access_bmap);
	reset_union_bmap_deny(od->od_share_deny, &stp->st_deny_bmap);

	update_stateid(&stp->st_stateid);
	memcpy(&od->od_stateid, &stp->st_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	if (od->od_stateowner) {
		nfs4_get_stateowner(od->od_stateowner);
		cstate->replay_owner = od->od_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * nfs4_unlock_state() called after encode
 */
__be32
nfsd4_close(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_close *close)
{
	__be32 status;
	struct nfs4_stateid *stp;
	struct super_block *sb;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

#if defined(CONFIG_SPNFS)
	/* temportary hook for spnfs testing purposes */
	sb = cstate->current_fh.fh_dentry->d_inode->i_sb;
	if (sb->s_export_op->close)
		sb->s_export_op->close(cstate->current_fh.fh_dentry->d_inode);
#endif /* CONFIG_SPNFS */

	nfs4_lock_state();
	/* check close_lru for replay */
	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					close->cl_seqid,
					&close->cl_stateid, 
					CHECK_FH | OPEN_STATE | CLOSE_STATE,
					&close->cl_stateowner, &stp, NULL)))
		goto out; 
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_stateid() calls nfsd_close() if needed */
	release_stateid(stp, OPEN_STATE);

	/* place unused nfs4_stateowners on so_close_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (list_empty(&close->cl_stateowner->so_stateids))
		move_to_close_lru(close->cl_stateowner);
out:
	if (close->cl_stateowner) {
		nfs4_get_stateowner(close->cl_stateowner);
		cstate->replay_owner = close->cl_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

__be32
nfsd4_delegreturn(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_delegreturn *dr)
{
	__be32 status;
	int flags = 0;

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		goto out;

	nfs4_lock_state();
	flags |= DELEG_RET;
	if (dr->dr_minorversion == 1)
		flags |= NFS_4_1;
	status = nfs4_preprocess_stateid_op(&cstate->current_fh,
					    &dr->dr_stateid, flags, NULL);
	nfs4_unlock_state();
out:
	return status;
}


/* 
 * Lock owner state (byte-range locks)
 */
#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))
#define LOCK_HASH_BITS              8
#define LOCK_HASH_SIZE             (1 << LOCK_HASH_BITS)
#define LOCK_HASH_MASK             (LOCK_HASH_SIZE - 1)

static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end: NFS4_LENGTH_EOF;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	BUG_ON(!len);
	end = start + len;
	return end > start ? end - 1: NFS4_LENGTH_EOF;
}

#define lockownerid_hashval(id) \
        ((id) & LOCK_HASH_MASK)

static inline unsigned int
lock_ownerstr_hashval(struct inode *inode, u32 cl_id,
		struct xdr_netobj *ownername)
{
	return (file_hashval(inode) + cl_id
			+ opaque_hashval(ownername->data, ownername->len))
		& LOCK_HASH_MASK;
}

static struct list_head lock_ownerid_hashtbl[LOCK_HASH_SIZE];
static struct list_head	lock_ownerstr_hashtbl[LOCK_HASH_SIZE];
static struct list_head lockstateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_stateid *
find_stateid(stateid_t *stid, int flags)
{
	struct nfs4_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("NFSD: find_stateid flags 0x%x\n",flags);
	if ((flags & LOCK_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &lockstateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} 
	if ((flags & OPEN_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &stateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	}
	return NULL;
}

static struct nfs4_delegation *
find_delegation_stateid(struct inode *ino, stateid_t *stid)
{
	struct nfs4_file *fp;
	struct nfs4_delegation *dl;

	dprintk("NFSD:find_delegation_stateid stateid=(%08x/%08x/%08x/%08x)\n",
                    stid->si_boot, stid->si_stateownerid,
                    stid->si_fileid, stid->si_generation);

	fp = find_file(ino);
	if (!fp)
		return NULL;
	dl = find_delegation_file(fp, stid);
	put_nfs4_file(fp);
	return dl;
}

/*
 * TODO: Linux file offsets are _signed_ 64-bit quantities, which means that
 * we can't properly handle lock requests that go beyond the (2^63 - 1)-th
 * byte, because of sign extension problems.  Since NFSv4 calls for 64-bit
 * locking, this prevents us from being completely protocol-compliant.  The
 * real solution to this problem is to start using unsigned file offsets in
 * the VFS, but this is a very deep change!
 */
static inline void
nfs4_transform_lock_offset(struct file_lock *lock)
{
	if (lock->fl_start < 0)
		lock->fl_start = OFFSET_MAX;
	if (lock->fl_end < 0)
		lock->fl_end = OFFSET_MAX;
}

/* Hack!: For now, we're defining this just so we can use a pointer to it
 * as a unique cookie to identify our (NFSv4's) posix locks. */
static struct lock_manager_operations nfsd_posix_mng_ops  = {
};

static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_stateowner *sop;
	unsigned int hval;

	if (fl->fl_lmops == &nfsd_posix_mng_ops) {
		sop = (struct nfs4_stateowner *) fl->fl_owner;
		hval = lockownerid_hashval(sop->so_id);
		kref_get(&sop->so_ref);
		deny->ld_sop = sop;
		deny->ld_clientid = sop->so_client->cl_clientid;
	} else {
		deny->ld_sop = NULL;
		deny->ld_clientid.cl_boot = 0;
		deny->ld_clientid.cl_id = 0;
	}
	deny->ld_start = fl->fl_start;
	deny->ld_length = NFS4_LENGTH_EOF;
	if (fl->fl_end != NFS4_LENGTH_EOF)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}

static struct nfs4_stateowner *
find_lockstateowner_str(struct inode *inode, clientid_t *clid,
		struct xdr_netobj *owner)
{
	unsigned int hashval = lock_ownerstr_hashval(inode, clid->cl_id, owner);
	struct nfs4_stateowner *op;

	list_for_each_entry(op, &lock_ownerstr_hashtbl[hashval], so_strhash) {
		if (same_owner_str(op, owner, clid))
			return op;
	}
	return NULL;
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occured. 
 *
 * strhashval = lock_ownerstr_hashval 
 */

static struct nfs4_stateowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfs4_stateid *open_stp, struct nfsd4_lock *lock) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&lock->lk_new_owner)))
		return NULL;
	idhashval = lockownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);
	INIT_LIST_HEAD(&sop->so_close_lru); /* not used */
	sop->so_time = 0;
	list_add(&sop->so_idhash, &lock_ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &lock_ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perstateid, &open_stp->st_lockowners);
	sop->so_is_open_owner = 0;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	/* It is the openowner seqid that will be incremented in encode in the
	 * case of new lockowners; so increment the lock seqid manually: */
	sop->so_seqid = lock->lk_new_lock_seqid + 1;
	sop->so_confirmed = 1;
	sop->so_minorversion = open_stp->st_stateowner->so_minorversion;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static struct nfs4_stateid *
alloc_init_lock_stateid(struct nfs4_stateowner *sop, struct nfs4_file *fp, struct nfs4_stateid *open_stp)
{
	struct nfs4_stateid *stp;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		goto out;
	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfile);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners); /* not used */
#if defined(CONFIG_PNFSD)
	INIT_LIST_HEAD(&stp->st_pnfs_ds_id);
#endif /* CONFIG_PNFSD */
	list_add(&stp->st_hash, &lockstateid_hashtbl[hashval]);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_vfs_file = open_stp->st_vfs_file; /* FIXME refcount?? */
	stp->st_access_bmap = open_stp->st_access_bmap;
	stp->st_deny_bmap = open_stp->st_deny_bmap;
	stp->st_openstp = open_stp;

out:
	return stp;
}

static int
check_lock_length(u64 offset, u64 length)
{
	return ((length == 0)  || ((length != NFS4_LENGTH_EOF) &&
	     LOFF_OVERFLOW(offset, length)));
}

/*
 *  LOCK operation 
 */
__be32
nfsd4_lock(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_lock *lock)
{
	struct nfs4_stateowner *open_sop = NULL;
	struct nfs4_stateowner *lock_sop = NULL;
	struct nfs4_stateid *lock_stp;
	struct file *filp;
	struct file_lock file_lock;
	struct file_lock conflock;
	__be32 status = 0;
	unsigned int strhashval;
	unsigned int cmd;
	int err;
#if defined(CONFIG_NFSD_V4_1)
	struct current_session *cses = cstate->current_ses;
#endif /* CONFIG_NFSD_V4_1 */

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		(long long) lock->lk_offset,
		(long long) lock->lk_length);

	if (check_lock_length(lock->lk_offset, lock->lk_length))
		 return nfserr_inval;

	if ((status = fh_verify(rqstp, &cstate->current_fh,
				S_IFREG, MAY_LOCK))) {
		dprintk("NFSD: nfsd4_lock: permission denied!\n");
		return status;
	}

	nfs4_lock_state();

	if (lock->lk_is_new) {
		/*
		 * Client indicates that this is a new lockowner.
		 * Use open owner and open stateid to create lock owner and
		 * lock stateid.
		 */
		struct nfs4_stateid *open_stp = NULL;
		struct nfs4_file *fp;
		
		status = nfserr_stale_clientid;
#if defined(CONFIG_NFSD_V4_1)
		if (!cses && STALE_CLIENTID(&lock->lk_new_clientid))
			goto out;
#endif /* CONFIG_NFSD_V4_1 */

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_seqid_op(&cstate->current_fh,
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
		                        CHECK_FH | OPEN_STATE,
		                        &lock->lk_replay_owner, &open_stp,
					lock);
		if (status)
			goto out;
		open_sop = lock->lk_replay_owner;
		/* create lockowner and lock stateid */
		fp = open_stp->st_file;
		strhashval = lock_ownerstr_hashval(fp->fi_inode, 
				open_sop->so_client->cl_clientid.cl_id, 
				&lock->v.new.owner);
		/* XXX: Do we need to check for duplicate stateowners on
		 * the same file, or should they just be allowed (and
		 * create new stateids)? */
		status = nfserr_resource;
		lock_sop = alloc_init_lock_stateowner(strhashval,
				open_sop->so_client, open_stp, lock);
		if (lock_sop == NULL)
			goto out;
		lock_stp = alloc_init_lock_stateid(lock_sop, fp, open_stp);
		if (lock_stp == NULL)
			goto out;
	} else {
		/* lock (lock owner + lock stateid) already exists */
		status = nfs4_preprocess_seqid_op(&cstate->current_fh,
				       lock->lk_old_lock_seqid, 
				       &lock->lk_old_lock_stateid, 
				       CHECK_FH | LOCK_STATE, 
				       &lock->lk_replay_owner, &lock_stp, lock);
		if (status)
			goto out;
		lock_sop = lock->lk_replay_owner;
	}
	/* lock->lk_replay_owner and lock_stp have been created or found */
	filp = lock_stp->st_vfs_file;

	status = nfserr_grace;
	if (nfs4_in_grace() && !lock->lk_reclaim)
		goto out;
	status = nfserr_no_grace;
	if (!nfs4_in_grace() && lock->lk_reclaim)
		goto out;

	locks_init_lock(&file_lock);
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
			cmd = F_SETLK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
			cmd = F_SETLK;
		break;
		default:
			status = nfserr_inval;
		goto out;
	}
	file_lock.fl_owner = (fl_owner_t)lock_sop;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX;
	file_lock.fl_lmops = &nfsd_posix_mng_ops;

	file_lock.fl_start = lock->lk_offset;
	file_lock.fl_end = last_byte_offset(lock->lk_offset, lock->lk_length);
	nfs4_transform_lock_offset(&file_lock);

	/*
	* Try to lock the file in the VFS.
	* Note: locks.c uses the BKL to protect the inode's lock list.
	*/

	/* XXX?: Just to divert the locks_release_private at the start of
	 * locks_copy_lock: */
	locks_init_lock(&conflock);
	err = vfs_lock_file(filp, cmd, &file_lock, &conflock);
	switch (-err) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stateid, 
				sizeof(stateid_t));
		status = 0;
		break;
	case (EAGAIN):		/* conflock holds conflicting lock */
		status = nfserr_denied;
		dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
		nfs4_set_lock_denied(&conflock, &lock->lk_denied);
		break;
	case (EDEADLK):
		status = nfserr_deadlock;
		break;
	default:        
		dprintk("NFSD: nfsd4_lock: vfs_lock_file() failed! status %d\n",err);
		status = nfserr_resource;
		break;
	}
out:
	if (status && lock->lk_is_new && lock_sop)
		release_stateowner(lock_sop);
	if (lock->lk_replay_owner) {
		nfs4_get_stateowner(lock->lk_replay_owner);
		cstate->replay_owner = lock->lk_replay_owner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * LOCKT operation
 */
__be32
nfsd4_lockt(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct file file;
	struct file_lock file_lock;
	int error;
	__be32 status;
#if defined(CONFIG_NFSD_V4_1)
	struct current_session *cses = cstate->current_ses;
#endif /* CONFIG_NFSD_V4_1 */

	if (nfs4_in_grace())
		return nfserr_grace;

	if (check_lock_length(lockt->lt_offset, lockt->lt_length))
		 return nfserr_inval;

	lockt->lt_stateowner = NULL;
	nfs4_lock_state();

	status = nfserr_stale_clientid;
#if defined(CONFIG_NFSD_V4_1)
	if (!cses && STALE_CLIENTID(&lockt->lt_clientid))
		goto out;
#endif /* CONFIG_NFSD_V4_1 */

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0))) {
		dprintk("NFSD: nfsd4_lockt: fh_verify() failed!\n");
		if (status == nfserr_symlink)
			status = nfserr_inval;
		goto out;
	}

	inode = cstate->current_fh.fh_dentry->d_inode;
	locks_init_lock(&file_lock);
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			dprintk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	lockt->lt_stateowner = find_lockstateowner_str(inode,
			&lockt->lt_clientid, &lockt->lt_owner);
	if (lockt->lt_stateowner)
		file_lock.fl_owner = (fl_owner_t)lockt->lt_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_flags = FL_POSIX;
	file_lock.fl_lmops = &nfsd_posix_mng_ops;

	file_lock.fl_start = lockt->lt_offset;
	file_lock.fl_end = last_byte_offset(lockt->lt_offset, lockt->lt_length);

	nfs4_transform_lock_offset(&file_lock);

	/* vfs_test_lock uses the struct file _only_ to resolve the inode.
	 * since LOCKT doesn't require an OPEN, and therefore a struct
	 * file may not exist, pass vfs_test_lock a struct file with
	 * only the dentry:inode set.
	 */
	memset(&file, 0, sizeof (struct file));
	file.f_path.dentry = cstate->current_fh.fh_dentry;

	status = nfs_ok;
	error = vfs_test_lock(&file, &file_lock);
	if (error) {
		status = nfserrno(error);
		goto out;
	}
	if (file_lock.fl_type != F_UNLCK) {
		status = nfserr_denied;
		nfs4_set_lock_denied(&file_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	return status;
}

__be32
nfsd4_locku(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_locku *locku)
{
	struct nfs4_stateid *stp;
	struct file *filp = NULL;
	struct file_lock file_lock;
	__be32 status;
	int err;
						        
	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		(long long) locku->lu_offset,
		(long long) locku->lu_length);

	if (check_lock_length(locku->lu_offset, locku->lu_length))
		 return nfserr_inval;

	nfs4_lock_state();
									        
	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					locku->lu_seqid, 
					&locku->lu_stateid, 
					CHECK_FH | LOCK_STATE, 
					&locku->lu_stateowner, &stp, NULL)))
		goto out;

	filp = stp->st_vfs_file;
	BUG_ON(!filp);
	locks_init_lock(&file_lock);
	file_lock.fl_type = F_UNLCK;
	file_lock.fl_owner = (fl_owner_t) locku->lu_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX; 
	file_lock.fl_lmops = &nfsd_posix_mng_ops;
	file_lock.fl_start = locku->lu_offset;

	file_lock.fl_end = last_byte_offset(locku->lu_offset, locku->lu_length);
	nfs4_transform_lock_offset(&file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	err = vfs_lock_file(filp, F_SETLK, &file_lock, NULL);
	if (err) {
		dprintk("NFSD: nfs4_locku: vfs_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stateid, sizeof(stateid_t));

out:
	if (locku->lu_stateowner) {
		nfs4_get_stateowner(locku->lu_stateowner);
		cstate->replay_owner = locku->lu_stateowner;
	}
	nfs4_unlock_state();
	return status;

out_nfserr:
	status = nfserrno(err);
	goto out;
}

/*
 * returns
 * 	1: locks held by lockowner
 * 	0: no locks held by lockowner
 */
static int
check_for_locks(struct file *filp, struct nfs4_stateowner *lowner)
{
	struct file_lock **flpp;
	struct inode *inode = filp->f_path.dentry->d_inode;
	int status = 0;

	lock_kernel();
	for (flpp = &inode->i_flock; *flpp != NULL; flpp = &(*flpp)->fl_next) {
		if ((*flpp)->fl_owner == (fl_owner_t)lowner) {
			status = 1;
			goto out;
		}
	}
out:
	unlock_kernel();
	return status;
}

__be32
nfsd4_release_lockowner(struct svc_rqst *rqstp,
			struct nfsd4_compound_state *cstate,
			struct nfsd4_release_lockowner *rlockowner)
{
	clientid_t *clid = &rlockowner->rl_clientid;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;
	struct xdr_netobj *owner = &rlockowner->rl_owner;
	struct list_head matches;
	int i;
	__be32 status;

	dprintk("nfsd4_release_lockowner clientid: (%08x/%08x):\n",
		clid->cl_boot, clid->cl_id);

	/* XXX check for lease expiration */

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		return status;

	nfs4_lock_state();

	status = nfserr_locks_held;
	/* XXX: we're doing a linear search through all the lockowners.
	 * Yipes!  For now we'll just hope clients aren't really using
	 * release_lockowner much, but eventually we have to fix these
	 * data structures. */
	INIT_LIST_HEAD(&matches);
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		list_for_each_entry(sop, &lock_ownerid_hashtbl[i], so_idhash) {
			if (!same_owner_str(sop, owner, clid))
				continue;
			list_for_each_entry(stp, &sop->so_stateids,
					st_perstateowner) {
				if (check_for_locks(stp->st_vfs_file, sop))
					goto out;
				/* Note: so_perclient unused for lockowners,
				 * so it's OK to fool with here. */
				list_add(&sop->so_perclient, &matches);
			}
		}
	}
	/* Clients probably won't expect us to return with some (but not all)
	 * of the lockowner state released; so don't release any until all
	 * have been checked. */
	status = nfs_ok;
	while (!list_empty(&matches)) {
		sop = list_entry(matches.next, struct nfs4_stateowner,
								so_perclient);
		/* unhash_stateowner deletes so_perclient only
		 * for openowners. */
		list_del(&sop->so_perclient);
		release_stateowner(sop);
	}
out:
	nfs4_unlock_state();
	return status;
}

static inline struct nfs4_client_reclaim *
alloc_reclaim(void)
{
	return kmalloc(sizeof(struct nfs4_client_reclaim), GFP_KERNEL);
}

int
nfs4_has_reclaimed_state(const char *name)
{
	unsigned int strhashval = clientstr_hashval(name);
	struct nfs4_client *clp;

	clp = find_confirmed_client_by_str(name, strhashval);
	return clp ? 1 : 0;
}

/*
 * failure => all reset bets are off, nfserr_no_grace...
 */
int
nfs4_client_to_reclaim(const char *name)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp = NULL;

	dprintk("NFSD nfs4_client_to_reclaim NAME: %.*s\n", HEXDIR_LEN, name);
	crp = alloc_reclaim();
	if (!crp)
		return 0;
	strhashval = clientstr_hashval(name);
	INIT_LIST_HEAD(&crp->cr_strhash);
	list_add(&crp->cr_strhash, &reclaim_str_hashtbl[strhashval]);
	memcpy(crp->cr_recdir, name, HEXDIR_LEN);
	reclaim_str_hashtbl_size++;
	return 1;
}

static void
nfs4_release_reclaim(void)
{
	struct nfs4_client_reclaim *crp = NULL;
	int i;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&reclaim_str_hashtbl[i])) {
			crp = list_entry(reclaim_str_hashtbl[i].next,
			                struct nfs4_client_reclaim, cr_strhash);
			list_del(&crp->cr_strhash);
			kfree(crp);
			reclaim_str_hashtbl_size--;
		}
	}
	BUG_ON(reclaim_str_hashtbl_size);
}

/*
 * called from OPEN, CLAIM_PREVIOUS with a new clientid. */
static struct nfs4_client_reclaim *
nfs4_find_reclaim_client(clientid_t *clid)
{
	unsigned int strhashval;
	struct nfs4_client *clp;
	struct nfs4_client_reclaim *crp = NULL;


	/* find clientid in conf_id_hashtbl */
	clp = find_confirmed_client(clid);
	if (clp == NULL)
		return NULL;

	dprintk("NFSD: nfs4_find_reclaim_client for %.*s with recdir %s\n",
		            clp->cl_name.len, clp->cl_name.data,
			    clp->cl_recdir);

	/* find clp->cl_name in reclaim_str_hashtbl */
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_for_each_entry(crp, &reclaim_str_hashtbl[strhashval], cr_strhash) {
		if (same_name(crp->cr_recdir, clp->cl_recdir)) {
			return crp;
		}
	}
	return NULL;
}

/*
* Called from OPEN. Look for clientid in reclaim list.
*/
__be32
nfs4_check_open_reclaim(clientid_t *clid)
{
	return nfs4_find_reclaim_client(clid) ? nfs_ok : nfserr_reclaim_bad;
}

/* initialization to perform at module load time: */

int
nfs4_state_init(void)
{
	int i, status;

	status = nfsd4_init_slabs();
	if (status)
		return status;
	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&conf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_id_hashtbl[i]);
	}
#if defined(CONFIG_NFSD_V4_1)
	for (i = 0; i < SESSION_HASH_SIZE; i++)
		INIT_LIST_HEAD(&sessionid_hashtbl[i]);
#endif /* CONFIG_NFSD_V4_1 */
	for (i = 0; i < FILE_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&file_hashtbl[i]);
	}
	for (i = 0; i < OWNER_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&ownerstr_hashtbl[i]);
		INIT_LIST_HEAD(&ownerid_hashtbl[i]);
	}
	for (i = 0; i < STATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&stateid_hashtbl[i]);
		INIT_LIST_HEAD(&lockstateid_hashtbl[i]);
	}
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&lock_ownerid_hashtbl[i]);
		INIT_LIST_HEAD(&lock_ownerstr_hashtbl[i]);
	}
	memset(&onestateid, ~0, sizeof(stateid_t));
	INIT_LIST_HEAD(&close_lru);
	INIT_LIST_HEAD(&client_lru);
	INIT_LIST_HEAD(&del_recall_lru);
	for (i = 0; i < CLIENT_HASH_SIZE; i++)
		INIT_LIST_HEAD(&reclaim_str_hashtbl[i]);
	reclaim_str_hashtbl_size = 0;
#if defined(CONFIG_PNFSD)
	nfs4_pnfs_state_init();
#endif /* CONFIG_PNFSD */
	return 0;
}

static void
nfsd4_load_reboot_recovery_data(void)
{
	int status;

	nfs4_lock_state();
	nfsd4_init_recdir(user_recovery_dirname);
	status = nfsd4_recdir_load();
	nfs4_unlock_state();
	if (status)
		printk("NFSD: Failure reading reboot recovery data\n");
}

unsigned long
get_nfs4_grace_period(void)
{
	return max(user_lease_time, lease_time) * HZ;
}

/*
 * Since the lifetime of a delegation isn't limited to that of an open, a
 * client may quite reasonably hang on to a delegation as long as it has
 * the inode cached.  This becomes an obvious problem the first time a
 * client's inode cache approaches the size of the server's total memory.
 *
 * For now we avoid this problem by imposing a hard limit on the number
 * of delegations, which varies according to the server's memory size.
 */
static void
set_max_delegations(void)
{
	/*
	 * Allow at most 4 delegations per megabyte of RAM.  Quick
	 * estimates suggest that in the worst case (where every delegation
	 * is for a different inode), a delegation could take about 1.5K,
	 * giving a worst case usage of about 6% of memory.
	 */
	max_delegations = nr_free_buffer_pages() >> (20 - 2 - PAGE_SHIFT);
}

/* initialization to perform when the nfsd service is started: */

static void
__nfs4_state_start(void)
{
	unsigned long grace_time;

	boot_time = get_seconds();
	grace_time = get_nfs_grace_period();
	lease_time = user_lease_time;
	in_grace = 1;
	printk(KERN_INFO "NFSD: starting %ld-second grace period\n",
	       grace_time/HZ);
	laundry_wq = create_singlethread_workqueue("nfsd4");
	queue_delayed_work(laundry_wq, &laundromat_work, grace_time);
	set_max_delegations();
}

void
nfs4_state_start(void)
{
	if (nfs4_init)
		return;
	nfsd4_load_reboot_recovery_data();
	__nfs4_state_start();
	nfs4_init = 1;
	return;
}

int
nfs4_in_grace(void)
{
	return in_grace;
}

time_t
nfs4_lease_time(void)
{
	return lease_time;
}

static void
__nfs4_state_shutdown(void)
{
	int i;
	struct nfs4_client *clp = NULL;
	struct nfs4_delegation *dp = NULL;
	struct list_head *pos, *next, reaplist;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&conf_id_hashtbl[i])) {
			clp = list_entry(conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			expire_client(clp);
		}
		while (!list_empty(&unconf_str_hashtbl[i])) {
			clp = list_entry(unconf_str_hashtbl[i].next, struct nfs4_client, cl_strhash);
			expire_client(clp);
		}
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}

	nfsd4_shutdown_recdir();
	nfs4_init = 0;
}

void
nfs4_state_shutdown(void)
{
	cancel_rearming_delayed_workqueue(laundry_wq, &laundromat_work);
	destroy_workqueue(laundry_wq);
	nfs4_lock_state();
	nfs4_release_reclaim();
	__nfs4_state_shutdown();
	nfs4_unlock_state();
}

static void
nfs4_set_recdir(char *recdir)
{
	nfs4_lock_state();
	strcpy(user_recovery_dirname, recdir);
	nfs4_unlock_state();
}

/*
 * Change the NFSv4 recovery directory to recdir.
 */
int
nfs4_reset_recoverydir(char *recdir)
{
	int status;
	struct nameidata nd;

	status = path_lookup(recdir, LOOKUP_FOLLOW, &nd);
	if (status)
		return status;
	status = -ENOTDIR;
	if (S_ISDIR(nd.path.dentry->d_inode->i_mode)) {
		nfs4_set_recdir(recdir);
		status = 0;
	}
	path_put(&nd.path);
	return status;
}

/*
 * Called when leasetime is changed.
 *
 * The only way the protocol gives us to handle on-the-fly lease changes is to
 * simulate a reboot.  Instead of doing that, we just wait till the next time
 * we start to register any changes in lease time.  If the administrator
 * really wants to change the lease time *now*, they can go ahead and bring
 * nfsd down and then back up again after changing the lease time.
 */
void
nfs4_reset_lease(time_t leasetime)
{
	lock_kernel();
	user_lease_time = leasetime;
	unlock_kernel();
}

#if defined(CONFIG_PNFSD)
static struct nfs4_layout_state *
alloc_init_layout_state(struct nfs4_client *clp, struct nfs4_file *fp,
			stateid_t *stateid)
{
	struct nfs4_layout_state *new;

	/* FIXME: use a kmem_cache */
	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return new;
	get_nfs4_file(fp);
	INIT_LIST_HEAD(&new->ls_perfile);
	INIT_LIST_HEAD(&new->ls_layouts);
	list_add(&new->ls_perfile, &fp->fi_layout_states);
	kref_init(&new->ls_ref);
	new->ls_client = clp;
	new->ls_file = fp;
	new->ls_stateid.si_boot = stateid->si_boot;
	new->ls_stateid.si_stateownerid = 0; /* identifies layout stateid */
	new->ls_stateid.si_fileid = current_layoutid++;
	new->ls_stateid.si_generation = 1;
	return new;
}

static inline void
get_layout_state(struct nfs4_layout_state *ls)
{
	kref_get(&ls->ls_ref);
}

static void
destroy_layout_state(struct kref *kref)
{
	struct nfs4_layout_state *ls =
			container_of(kref, struct nfs4_layout_state, ls_ref);
	struct nfs4_file *fp = ls->ls_file;

	dprintk("pNFS %s: ls %p fp %p clp %p\n", __func__, ls, fp,
				ls->ls_client);
	BUG_ON(!list_empty(&ls->ls_layouts));
	list_del(&ls->ls_perfile);
	kfree(ls);
	put_nfs4_file(fp);
}

static inline void
put_layout_state(struct nfs4_layout_state *ls)
{
	dprintk("pNFS %s: ls %p ls_ref %d\n", __func__, ls,
				atomic_read(&ls->ls_ref.refcount));
	kref_put(&ls->ls_ref, destroy_layout_state);
}

/*
 * Search the fp->fi_layout_state list for a layout state with the clientid.
 * If not found, then this is a 'first open/delegation/lock stateid' from
 * the client for this file.
 */
struct nfs4_layout_state *
find_get_layout_state(struct nfs4_client *clp, struct nfs4_file *fp)
{
	struct nfs4_layout_state *ls;

	BUG_ON_UNLOCKED_STATE();
	list_for_each_entry(ls, &fp->fi_layout_states, ls_perfile) {
		if (ls->ls_client == clp) {
			dprintk("pNFS %s: before GET ls %p ls_ref %d\n",
					__func__, ls,
					atomic_read(&ls->ls_ref.refcount));
			get_layout_state(ls);
			return ls;
		}
	}
	return NULL;
}

static int
verify_stateid(struct nfs4_file *fp, stateid_t *stateid)
{
	struct nfs4_stateid *local = NULL;
	struct nfs4_delegation *temp = NULL;

	/* check if open or lock stateid */
	local = find_stateid(stateid, RD_STATE);
	if (local)
		return 0;
	temp = find_delegation_stateid(fp->fi_inode, stateid);
	if (temp)
		return 0;
	return nfserr_bad_stateid;
}

/*
 * nfs4_preocess_layout_stateid ()
 *
 * We have looked up the nfs4_file corresponding to the current_fh, and
 * confirmed the clientid. Pull the few tests from nfs4_preprocess_stateid_op()
 * that make sense with a layout stateid.
 *
 * Called with the nfs_lock held.
 * Returns zero and stateid is updated, or error.
 *
 * Note: the struct nfs4_layout_state pointer is only set by layoutget.
 */
static __be32
nfs4_process_layout_stateid(struct nfs4_client *clp, struct nfs4_file *fp,
			    stateid_t *stateid, struct nfs4_layout_state **lsp)
{
	struct nfs4_layout_state *ls = NULL;
	int status = 0;

	dprintk("--> %s clp %p fp %p \n", __func__, clp, fp);

	dprintk("%s:  operation stateid=(%08x/%08x/%08x/%08x)\n\n", __func__,
					stateid->si_boot,
					stateid->si_stateownerid,
					stateid->si_fileid,
					stateid->si_generation);

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid))
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		goto out;

	/* Is this the first use of this layout ? */
	ls = find_get_layout_state(clp, fp);
	if (!ls) {
		/* Only alloc layout state on layoutget (which sets lsp). */
		if (!lsp) {
			dprintk("%s ERROR: Not layoutget & no layout stateid\n",
							__func__);
			status = nfserr_bad_stateid;
			goto out;
		}
		dprintk("%s Initial stateid for layout: file %p client %p\n",
				__func__, fp, clp);

		/* verify input stateid */
		status = verify_stateid(fp, stateid);
		if (status < 0) {
			dprintk("%s ERROR: invalid open/deleg/lock stateid\n",
							__func__);
			goto out;
		}
		ls = alloc_init_layout_state(clp, fp, stateid);
		if (!ls) {
			dprintk("%s pNFS ERROR: no memory for layout state\n",
							__func__);
			status = nfserr_resource;
			goto out;
		}
		dprintk("pNFS %s: before GET ls %p ls_ref %d\n",
					__func__, ls,
					atomic_read(&ls->ls_ref.refcount));
		get_layout_state(ls);
	} else {
		dprintk("%s Not initial stateid. Layout state %p file %p\n",
						__func__, ls, fp);

		/* BAD STATEID */
		status = nfserr_bad_stateid;
		if (memcmp(&ls->ls_stateid.si_opaque, &stateid->si_opaque,
		    sizeof(stateid_opaque_t)) != 0) {

			/* if a LAYOUTGET operation and stateid is a valid
			* open/deleg/lock stateid, accept it as a parallel
			* initial layout stateid
			*/
			if (lsp && ((verify_stateid(fp, stateid)) == 0)) {
				dprintk("%s parallel initial layout state\n",
								__func__);
				goto update;
			}

			dprintk("%s ERROR bad opaque in stateid 1\n", __func__);
			goto out_put;
		}

		/* stateid is a valid layout stateid for this file. */
		if (stateid->si_generation > ls->ls_stateid.si_generation) {
			dprintk("%s bad stateid 1\n", __func__);
			goto out_put;
		}
update:
		update_stateid(&ls->ls_stateid);
		dprintk("%s Updated ls_stateid to %d on layoutstate %p\n",
				__func__, ls->ls_stateid.si_generation, ls);
	}
	status = 0;
	/* Set the stateid to be encoded */
	memcpy(stateid, &ls->ls_stateid, sizeof(stateid_t));

/* Return the layout state if requested */
	if (lsp)
		*lsp = ls;
out_put:
	dprintk("%s PUT LO STATE:\n", __func__);
	put_layout_state(ls);
out:
	dprintk("<-- %s status %d\n", __func__, htonl(status));
	dprintk("%s: layout stateid=(%08x/%08x/%08x/%08x)\n\n", __func__,
					ls->ls_stateid.si_boot,
					ls->ls_stateid.si_stateownerid,
					ls->ls_stateid.si_fileid,
					ls->ls_stateid.si_generation);

	return status;
}

static inline struct nfs4_layout *
alloc_layout(void)
{
	return kmem_cache_alloc(pnfs_layout_slab, GFP_KERNEL);
}

static inline void
free_layout(struct nfs4_layout *lp)
{
	kmem_cache_free(pnfs_layout_slab, lp);
}

static void
init_layout(struct nfs4_layout_state *ls,
	    struct nfs4_layout *lp,
	    struct nfs4_file *fp,
	    struct nfs4_client *clp,
	    struct svc_fh *current_fh,
	    struct nfsd4_layout_seg *seg)
{
	dprintk("pNFS %s: ls %p lp %p clp %p fp %p ino %p\n", __func__,
		ls, lp, clp, fp, fp->fi_inode);

	get_nfs4_file(fp);
	lp->lo_client = clp;
	lp->lo_file = fp;
	get_layout_state(ls);
	lp->lo_state = ls;
	memcpy(&lp->lo_seg, seg, sizeof(lp->lo_seg));
	list_add_tail(&lp->lo_perstate, &ls->ls_layouts);
	list_add_tail(&lp->lo_perclnt, &clp->cl_layouts);
	list_add_tail(&lp->lo_perfile, &fp->fi_layouts);
	dprintk("pNFS %s end\n", __func__);
}

static void
destroy_layout(struct nfs4_layout *lp)
{
	struct nfs4_client *clp;
	struct nfs4_file *fp;
	struct nfs4_layout_state *ls;

	list_del(&lp->lo_perclnt);
	list_del(&lp->lo_perfile);
	list_del(&lp->lo_perstate);
	clp = lp->lo_client;
	fp = lp->lo_file;
	ls = lp->lo_state;
	dprintk("pNFS %s: lp %p clp %p fp %p ino %p ls_layouts empty %d\n",
		__func__, lp, clp, fp, fp->fi_inode,
		list_empty(&ls->ls_layouts));

	kmem_cache_free(pnfs_layout_slab, lp);
	put_layout_state(ls);
	if (list_empty(&ls->ls_layouts))
		put_layout_state(ls); /* Final put */
	put_nfs4_file(fp);
}

static int
expire_layout(struct nfs4_layout *lp)
{
	struct nfs4_client *clp;
	struct nfs4_file *fp;
	struct nfsd4_pnfs_layoutreturn lr;

	clp = lp->lo_client;
	fp = lp->lo_file;
	dprintk("pNFS %s: lp %p clp %p fp %p ino %p\n", __func__,
		lp, clp, fp, fp->fi_inode);

	/* call exported filesystem layout_return */
	if (!fp->fi_inode->i_sb->s_export_op->layout_return)
		return 0;

	lr.lr_return_type = RETURN_FILE;
	lr.lr_reclaim = 0;
	lr.lr_flags = LR_FLAG_EXPIRE;
	lr.lr_seg.clientid = lp->lo_seg.clientid;
	lr.lr_seg.layout_type = lp->lo_seg.layout_type;
	lr.lr_seg.iomode = IOMODE_ANY;
	lr.lr_seg.offset = 0;
	lr.lr_seg.length = NFS4_LENGTH_EOF;
	return fp->fi_inode->i_sb->s_export_op->layout_return(
		fp->fi_inode, &lr);
}

/*
 * Create a layoutrecall structure
 * An optional layoutrecall can be cloned (except for the layoutrecall lists)
 */
static struct nfs4_layoutrecall *
alloc_init_layoutrecall(struct nfs4_layoutrecall *clone)
{
	struct nfs4_layoutrecall *clr;

	dprintk("NFSD %s\n", __func__);
	clr = kmem_cache_alloc(pnfs_layoutrecall_slab, GFP_KERNEL);
	if (clr == NULL)
		return clr;

	dprintk("NFSD %s clr %p clone %p\n", __func__, clr, clone);

	if (clone) {
		memcpy(clr, clone, sizeof(*clr));
		if (clr->clr_file)
			get_nfs4_file(clr->clr_file);
	} else
		memset(clr, 0, sizeof(*clr));
	kref_init(&clr->clr_ref);
	INIT_LIST_HEAD(&clr->clr_perclnt);

	dprintk("NFSD %s return %p\n", __func__, clr);
	return clr;
}

static void
hash_layoutrecall(struct nfs4_layoutrecall *clr)
{
	struct nfs4_client *clp = clr->clr_client;
	struct nfs4_file *fp = clr->clr_file;

	dprintk("NFSD %s clr %p clp %p fp %p\n", __func__, clr, clp, fp);
	list_add(&clr->clr_perclnt, &clp->cl_layoutrecalls);
	kref_get(&clr->clr_ref);
	dprintk("NFSD %s exit\n", __func__);
}

static void
destroy_layoutrecall(struct kref *kref)
{
	struct nfs4_layoutrecall *clr =
		container_of(kref, struct nfs4_layoutrecall, clr_ref);
	dprintk("pNFS %s: clr %p fp %p clp %p\n", __func__, clr,
		clr->clr_file, clr->clr_client);
	BUG_ON(!list_empty(&clr->clr_perclnt));
	if (clr->clr_file)
		put_nfs4_file(clr->clr_file);
	kmem_cache_free(pnfs_layoutrecall_slab, clr);
}

static inline void
put_layoutrecall(struct nfs4_layoutrecall *clr)
{
	dprintk("pNFS %s: clr %p clr_ref %d\n", __func__, clr,
		atomic_read(&clr->clr_ref.refcount));
	kref_put(&clr->clr_ref, destroy_layoutrecall);
}

static void
layoutrecall_done(struct nfs4_layoutrecall *clr)
{
	dprintk("pNFS %s: clr %p clr_ref %d\n", __func__, clr,
		atomic_read(&clr->clr_ref.refcount));
	list_del_init(&clr->clr_perclnt);
	put_layoutrecall(clr);
}

/*
 * get_state() and cb_get_state() are
 */
static void
release_pnfs_ds_dev_list(struct nfs4_stateid *stp)
{
	struct pnfs_ds_dev_entry *ddp;

	while (!list_empty(&stp->st_pnfs_ds_id)) {
		ddp = list_entry(stp->st_pnfs_ds_id.next,
				struct pnfs_ds_dev_entry, dd_dev_entry);
		list_del(&ddp->dd_dev_entry);
		kfree(ddp);
	}
}

static int
nfs4_add_pnfs_ds_dev(struct nfs4_stateid *stp, u32 dsid)
{
	struct pnfs_ds_dev_entry *ddp;

	ddp = kmalloc(sizeof(*ddp), GFP_KERNEL);
	if (!ddp)
		return -ENOMEM;

	INIT_LIST_HEAD(&ddp->dd_dev_entry);
	list_add(&ddp->dd_dev_entry, &stp->st_pnfs_ds_id);
	ddp->dd_dsid = dsid;
	return 0;
}

/*
 * are two octet ranges overlapping?
 * start1            last1
 *   |-----------------|
 *                start2            last2
 *                  |----------------|
 */
static inline int
lo_seg_overlapping(struct nfsd4_layout_seg *l1, struct nfsd4_layout_seg *l2)
{
	u64 start1 = l1->offset;
	u64 last1 = last_byte_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 last2 = last_byte_offset(start2, l2->length);
	int ret;

	/* if last1 == start2 there's a single byte overlap */
	ret = (last2 >= start1) && (last1 >= start2);
	dprintk("%s: l1 %llu:%lld l2 %llu:%lld ret=%d\n", __func__,
		l1->offset, l1->length, l2->offset, l2->length, ret);
	return ret;
}

static inline int
same_fsid_major(struct nfs4_fsid *fsid, u64 major)
{
	return fsid->major == major;
}

static inline int
same_fsid(struct nfs4_fsid *fsid, struct svc_fh *current_fh)
{
	return same_fsid_major(fsid, current_fh->fh_export->ex_fsid);
}

/*
 * find a layout recall conflicting with the specified layoutget
 */
static int
is_layout_recalled(struct nfs4_client *clp,
		   struct svc_fh *current_fh,
		   struct nfsd4_layout_seg *seg)
{
	struct nfs4_layoutrecall *clr;

	list_for_each_entry (clr, &clp->cl_layoutrecalls, clr_perclnt) {
		if (clr->cb.cbl_seg.layout_type != seg->layout_type)
			continue;
		if (clr->cb.cbl_recall_type == RECALL_ALL)
			return 1;
		if (clr->cb.cbl_recall_type == RECALL_FSID) {
			if (same_fsid(&clr->cb.cbl_fsid, current_fh))
				return 1;
			else
				continue;
		}
		BUG_ON(clr->cb.cbl_recall_type != RECALL_FILE);
		if (clr->cb.cbl_seg.clientid == seg->clientid &&
		    lo_seg_overlapping(&clr->cb.cbl_seg, seg))
			return 1;
	}
	return 0;
}

/*
 * are two octet ranges overlapping or adjacent?
 */
static inline int
lo_seg_mergeable(struct nfsd4_layout_seg *l1, struct nfsd4_layout_seg *l2)
{
	u64 start1 = l1->offset;
	u64 end1 = end_offset(start1, l1->length);
	u64 start2 = l2->offset;
	u64 end2 = end_offset(start2, l2->length);

	/* is end1 == start2 ranges are adjacent */
	return (end2 >= start1) && (end1 >= start2);
}

static void
extend_layout(struct nfsd4_layout_seg *lo, struct nfsd4_layout_seg *lg)
{
	u64 lo_start = lo->offset;
	u64 lo_end = end_offset(lo_start, lo->length);
	u64 lg_start = lg->offset;
	u64 lg_end = end_offset(lg_start, lg->length);

	/* lo already covers lg? */
	if (lo_start <= lg_start && lg_end <= lo_end)
		return;

	/* extend start offset */
	if (lo_start > lg_start)
		lo_start = lg_start;

	/* extend end offset */
	if (lo_end < lg_end)
		lo_end = lg_end;

	lo->offset = lo_start;
	lo->length = (lo_end == NFS4_LENGTH_EOF) ?
			 lo_end : lo_end - lo_start;
}

static struct nfs4_layout *
merge_layout(struct nfs4_file *fp,
	     struct nfs4_client *clp,
	     struct nfsd4_layout_seg *seg)
{
	struct nfs4_layout *lp;

	list_for_each_entry (lp, &fp->fi_layouts, lo_perfile)
		if (lp->lo_seg.layout_type == seg->layout_type &&
		    lp->lo_seg.clientid == seg->clientid &&
		    lp->lo_seg.iomode == seg->iomode &&
		    lo_seg_mergeable(&lp->lo_seg, seg)) {
			extend_layout(&lp->lo_seg, seg);
			return lp;
		}

	return NULL;
}

int
nfs4_pnfs_get_layout(struct svc_fh *current_fh,
		     struct pnfs_layoutget_arg *args,
		     stateid_t *stateid)
{
	int status = nfserr_layouttrylater;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct super_block *sb = ino->i_sb;
	int can_merge;
	struct nfs4_file *fp;
	struct nfs4_client *clp;
	struct nfs4_layout *lp = NULL;
	struct nfs4_layout_state *ls = NULL;

	dprintk("NFSD: %s Begin\n", __func__);

	nfs4_lock_state();
	fp = find_alloc_file(ino, current_fh);
	clp = find_confirmed_client((clientid_t *)&args->seg.clientid);
	dprintk("pNFS %s: fp %p clp %p \n", __func__, fp, clp);
	if (!fp || !clp)
		goto out;

	/* Check decoded layout stateid */
	status = nfs4_process_layout_stateid(clp, fp, stateid, &ls);
	if (status)
		goto out;

	if (is_layout_recalled(clp, current_fh, &args->seg)) {
		status = nfserr_recallconflict;
		goto out;
	}

	can_merge = sb->s_export_op->can_merge_layouts != NULL &&
		    sb->s_export_op->can_merge_layouts(args->seg.layout_type);

	/* pre-alloc layout in case we can't merge after we call
	 * the file system
	 */
	lp = alloc_layout();
	if (!lp)
		goto out;

	dprintk("pNFS %s: pre-export type 0x%x maxcount %d "
		"iomode %u offset %llu length %llu\n",
		__func__, args->seg.layout_type, args->xdr.maxcount,
		args->seg.iomode, args->seg.offset, args->seg.length);

	status = sb->s_export_op->layout_get(ino, args);

	dprintk("pNFS %s: post-export status %d "
		"iomode %u offset %llu length %llu\n",
		__func__, status, args->seg.iomode,
		args->seg.offset, args->seg.length);

	if (status) {
		switch (status) {
		case -ENOMEM:
		case -EAGAIN:
		case -EINTR:
			status = nfserr_layouttrylater;
			break;
		case -ENOENT:
			status = nfserr_badlayout;
			break;
		case -E2BIG:
			status = nfserr_toosmall;
			break;
		default:
			status = nfserr_layoutunavailable;
		}
		goto out_freelayout;
	}

	/* SUCCESS!
	 * Can the new layout be merged into an existing one?
	 * If so, free unused layout struct
	 */
	if (can_merge && merge_layout(fp, clp, &args->seg))
		goto out_freelayout;

	/* Can't merge, so let's initialize this new layout */
	init_layout(ls, lp, fp, clp, current_fh, &args->seg);
out:
	if (fp)
		put_nfs4_file(fp);
	nfs4_unlock_state();
	dprintk("pNFS %s: lp %p exit status %d\n", __func__, lp, status);
	return status;
out_freelayout:
	free_layout(lp);
	goto out;
}

static void
trim_layout(struct nfsd4_layout_seg *lo, struct nfsd4_layout_seg *lr)
{
	u64 lo_start = lo->offset;
	u64 lo_end = end_offset(lo_start, lo->length);
	u64 lr_start = lr->offset;
	u64 lr_end = end_offset(lr_start, lr->length);

	dprintk("%s:Begin lo %llu:%lld lr %llu:%lld\n", __func__,
		lo->offset, lo->length, lr->offset, lr->length);

	/* lr fully covers lo? */
	if (lr_start <= lo_start && lo_end <= lr_end) {
		lo->length = 0;
		goto out;
	}

	/*
	 * split not supported yet. retain layout segment.
	 * remains must be returned by the client
	 * on the final layout return.
	 */
	if (lo_start < lr_start && lr_end < lo_end) {
		dprintk("%s: split not supported\n", __func__);
		goto out;
	}

	if (lo_start < lr_start)
		lo_end = lr_start - 1;
	else /* lr_end < lo_end */
		lo_start = lr_end + 1;

	lo->offset = lo_start;
	lo->length = (lo_end == NFS4_LENGTH_EOF) ? lo_end : lo_end - lo_start;
out:
	dprintk("%s:End lo %llu:%lld\n", __func__, lo->offset, lo->length);
}

static int
pnfs_return_file_layouts(struct nfs4_client *clp, struct nfs4_file *fp,
			 struct nfsd4_pnfs_layoutreturn *lrp)
{
	int layouts_found = 0;
	struct nfs4_layout *lp, *nextlp;

	dprintk("%s: clp %p fp %p\n", __func__, clp, fp);
	list_for_each_entry_safe (lp, nextlp, &fp->fi_layouts, lo_perfile) {
		dprintk("%s: lp %p client %p,%p lo_type %x,%x iomode %d,%d\n",
			__func__, lp,
			lp->lo_client, clp,
			lp->lo_seg.layout_type, lrp->lr_seg.layout_type,
			lp->lo_seg.iomode, lrp->lr_seg.iomode);
		if (lp->lo_client != clp ||
		    lp->lo_seg.layout_type != lrp->lr_seg.layout_type ||
		    (lp->lo_seg.iomode != lrp->lr_seg.iomode &&
		     lrp->lr_seg.iomode != IOMODE_ANY) ||
		    !lo_seg_overlapping(&lp->lo_seg, &lrp->lr_seg))
			continue;
		layouts_found++;
		trim_layout(&lp->lo_seg, &lrp->lr_seg);
		if (!lp->lo_seg.length)
			destroy_layout(lp);
	}

	return layouts_found;
}

static int
pnfs_return_client_layouts(struct nfs4_client *clp,
			   struct nfsd4_pnfs_layoutreturn *lrp, u64 ex_fsid)
{
	int layouts_found = 0;
	struct nfs4_layout *lp, *nextlp;

	list_for_each_entry_safe (lp, nextlp, &clp->cl_layouts, lo_perclnt) {
		if (lrp->lr_seg.layout_type != lp->lo_seg.layout_type ||
		    (lrp->lr_seg.iomode != lp->lo_seg.iomode &&
		     lrp->lr_seg.iomode != IOMODE_ANY))
			continue;

		if (lrp->lr_return_type == RETURN_FSID &&
		    !same_fsid_major(&lp->lo_file->fi_fsid, ex_fsid))
			continue;

		layouts_found++;
		destroy_layout(lp);
	}

	return layouts_found;
}

static int
recall_return_perfect_match(struct nfs4_layoutrecall *clr,
			    struct nfsd4_pnfs_layoutreturn *lrp,
			    struct nfs4_file *fp,
			    struct svc_fh *current_fh)
{
	if (clr->cb.cbl_seg.iomode != lrp->lr_seg.iomode ||
	    clr->cb.cbl_recall_type != lrp->lr_return_type)
		return 0;

	return (clr->cb.cbl_recall_type == RECALL_FILE &&
		clr->clr_file == fp &&
		clr->cb.cbl_seg.offset == lrp->lr_seg.offset &&
		clr->cb.cbl_seg.length == lrp->lr_seg.length) ||

		(clr->cb.cbl_recall_type == RECALL_FSID &&
		 same_fsid(&clr->cb.cbl_fsid, current_fh)) ||

		clr->cb.cbl_recall_type == RECALL_ALL;
}

static int
recall_return_partial_match(struct nfs4_layoutrecall *clr,
			    struct nfsd4_pnfs_layoutreturn *lrp,
			    struct nfs4_file *fp,
			    struct svc_fh *current_fh)
{
	/* iomode matching? */
	if (clr->cb.cbl_seg.iomode != lrp->lr_seg.iomode &&
	    clr->cb.cbl_seg.iomode != IOMODE_ANY &&
	    lrp->lr_seg.iomode != IOMODE_ANY)
		return 0;

	if (clr->cb.cbl_recall_type == RECALL_ALL ||
	    lrp->lr_return_type == RETURN_ALL)
		return 1;

	/* fsid matches? */
	if (clr->cb.cbl_recall_type == RECALL_FSID ||
	    lrp->lr_return_type == RETURN_FSID)
		return same_fsid(&clr->cb.cbl_fsid, current_fh);

	/* file matches, range overlapping? */
	return clr->clr_file == fp &&
	       lo_seg_overlapping(&clr->cb.cbl_seg, &lrp->lr_seg);
}

int nfs4_pnfs_return_layout(struct super_block *sb, struct svc_fh *current_fh,
				struct nfsd4_pnfs_layoutreturn *lrp)
{
	int status = -ENOENT;
	int layouts_found = 0;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp = NULL;
	struct nfs4_client *clp = NULL;
	struct nfs4_layoutrecall *clr, *nextclr;

	dprintk("NFSD: %s\n", __func__);

	/* call exported filesystem layout_return first */
	if (sb->s_export_op->layout_return) {
		status = sb->s_export_op->layout_return(ino, lrp);
		if (status)
			goto out_unlocked;
	}

	nfs4_lock_state();
	clp = find_confirmed_client((clientid_t *)&lrp->lr_seg.clientid);
	if (!clp)
		goto out;
	fp = find_file(ino);
	if (!fp)
		goto out;

	/* Check the stateid */
	dprintk("%s PROCESS LO_STATEID inode %p\n", __func__, ino);
	status = nfs4_process_layout_stateid(clp, fp, &lrp->lr_sid, NULL);
	if (status)
		goto out;

	/* update layouts */
	layouts_found += lrp->lr_return_type == RETURN_FILE ?
		pnfs_return_file_layouts(clp, fp, lrp) :
		pnfs_return_client_layouts(clp, lrp,
					   current_fh->fh_export->ex_fsid);

	dprintk("pNFS %s: clp %p fp %p layout_type 0x%x iomode %d "
		"return_type %d fsid 0x%x offset %lld length %lld: "
		"layouts_found %d\n",
		__func__, clp, fp, lrp->lr_seg.layout_type,
		lrp->lr_seg.iomode, lrp->lr_return_type,
		current_fh->fh_export->ex_fsid,
		lrp->lr_seg.offset, lrp->lr_seg.length, layouts_found);

	/* update layoutrecalls */
	list_for_each_entry_safe (clr, nextclr, &clp->cl_layoutrecalls,
				  clr_perclnt) {
		if (clr->cb.cbl_seg.layout_type != lrp->lr_seg.layout_type)
			continue;

		if (recall_return_perfect_match(clr, lrp, fp, current_fh))
			layoutrecall_done(clr);
		else if (layouts_found &&
			 recall_return_partial_match(clr, lrp, fp, current_fh))
			clr->clr_time = CURRENT_TIME;
	}

out:
	if (fp)
		put_nfs4_file(fp);
	nfs4_unlock_state();
out_unlocked:
	dprintk("pNFS %s: exit status %d \n", __func__, status);
	return status;
}


/*
 * PNFS Metadata server export operations callback for get_state
 *
 * called by the cluster fs when it receives a get_state() from a data
 * server.
 * returns status, or pnfs_get_state* with pnfs_get_state->status set.
 *
 */
int
nfs4_pnfs_cb_get_state(struct super_block *sb, struct pnfs_get_state *arg)
{
	struct nfs4_stateid *stp;
	int flags = LOCK_STATE | OPEN_STATE; /* search both hash tables */
	int status = -EINVAL;
	struct inode *ino;
	struct nfs4_delegation *dl;

	dprintk("NFSD: %s sid=(%08x/%08x/%08x/%08x) ion %ld\n\n",
				__func__,
				arg->stid.si_boot,
				arg->stid.si_stateownerid,
				arg->stid.si_fileid,
				arg->stid.si_generation,
				arg->ino);

	nfs4_lock_state();
	stp = find_stateid(&arg->stid, flags);
	if (!stp) {
		ino = iget_locked(sb, arg->ino);
		if (!ino)
			goto out;

		if (ino->i_state & I_NEW) {
			iget_failed(ino);
			goto out;
		}

		dl = find_delegation_stateid(ino, &arg->stid);
		if (dl)
			status = 0;

		iput(ino);
	} else {
		/* XXX ANDROS: marc removed nfs4_check_fh - how come? */

		/* arg->devid is the Data server id, set by the cluster fs */
		status = nfs4_add_pnfs_ds_dev(stp, arg->dsid);
		if (status)
			goto out;

		arg->access = stp->st_access_bmap;
		arg->clid = stp->st_stateowner->so_client->cl_clientid;
	}
out:
	nfs4_unlock_state();
	return status;
}

static int
cl_has_file_layout(struct nfs4_client *clp, struct nfs4_layoutrecall *clr)
{
	struct nfs4_layout *lp;

	list_for_each_entry(lp, &clp->cl_layouts, lo_perclnt)
		if (lp->lo_file == clr->clr_file)
			return 1;

	return 0;
}

static int
cl_has_fsid_layout(struct nfs4_client *clp, struct nfs4_layoutrecall *clr)
{
	struct nfs4_layout *lp;

	/* note: minor version unused */
	list_for_each_entry(lp, &clp->cl_layouts, lo_perclnt)
		if (lp->lo_file->fi_fsid.major == clr->cb.cbl_fsid.major)
			return 1;

	return 0;
}

static int
cl_has_any_layout(struct nfs4_client *clp, struct nfs4_layoutrecall *dummy)
{
	return !list_empty(&clp->cl_layouts);
}

#if 0
/*
 * Recall a layout asynchronously
 * FIXME: Failures are nor reported back
 */
static int
do_layout_recall(void *__clr)
{
	struct nfs4_layoutrecall *pending, *clr = __clr;
	struct nfs4_client *clp = NULL;
	unsigned int i;
	int status;
	struct list_head todolist;
	static int (*__has_layout)(struct nfs4_client *,
				   struct nfs4_layoutrecall *);

	daemonize("nfsv4-layout");

	INIT_LIST_HEAD(&todolist);

	/* specific client */
	if (clr->clr_client) {
		list_add(&clr->clr_perclnt, &todolist);
		clr = NULL;	/* so it won't be destroyed here */
		goto doit;
	}

	switch (clr->cb.cbl_recall_type) {
	case RECALL_FILE:
		__has_layout = cl_has_file_layout;
		break;
	case RECALL_FSID:
		__has_layout = cl_has_fsid_layout;
		break;
	case RECALL_ALL:
		__has_layout = cl_has_any_layout;
		break;
	}

	nfs4_lock_state();
	for (i = 0; i < CLIENT_HASH_SIZE; i++)
		list_for_each_entry(clp, &conf_str_hashtbl[i], cl_strhash)
			if (__has_layout(clp, clr)) {
				pending = alloc_init_layoutrecall(clr);
				if (!pending)
					goto doit;
				pending->clr_client = clp;
				list_add(&pending->clr_perclnt, &todolist);
			}
	/* cleanup only in the multi client, single client went into todolist */
	put_layoutrecall(clr);
	nfs4_unlock_state();

doit:
	while (!list_empty(&todolist)) {
		pending = list_entry(todolist.next, struct nfs4_layoutrecall,
				     clr_perclnt);
		list_del_init(&pending->clr_perclnt);
		dprintk("%s: clp %p cb_client %p fp %p\n", __func__,
			pending->clr_client,
			pending->clr_client->cl_callback.cb_client,
			pending->clr_file);
		if (unlikely(!pending->clr_client->cl_callback.cb_client)) {
			printk(KERN_WARNING
			       "%s: clientid %llx has no callback path\n",
			       __func__,
			       (unsigned long long)pending->cb.cbl_seg.clientid);
			nfs4_lock_state();
			put_layoutrecall(pending);
			nfs4_unlock_state();
			continue;
		}
		pending->clr_time = CURRENT_TIME;
		nfs4_lock_state();
		hash_layoutrecall(pending);
		nfs4_unlock_state();

		status = nfsd4_cb_layout(pending);
		/* if (status && status != NFSERR_NOMATCHING_LAYOUT) */
			printk(KERN_WARNING "%s: clp %p cb_client %p fp %p "
			       "failed with status %d\n", __func__,
				pending->clr_client,
				pending->clr_client->cl_callback.cb_client,
				pending->clr_file,
				status);

		nfs4_lock_state();
		put_layoutrecall(pending);
		nfs4_unlock_state();
	}

	return 0;
}
#endif

static void
nomatching_layout(struct super_block *sb, struct nfs4_layoutrecall *clr)
{
	struct nfsd4_pnfs_layoutreturn lr;

	dprintk("%s: clp %p fp %p: "
		"simulating layout_return\n", __func__,
		clr->clr_client,
		clr->clr_file);
	lr.lr_return_type = clr->cb.cbl_recall_type;
	lr.lr_seg = clr->cb.cbl_seg;
	lr.lr_reclaim = 0;
	lr.lr_flags = LR_FLAG_INTERN;
	if (sb->s_export_op->layout_return)
		sb->s_export_op->layout_return(clr->clr_file ?
			clr->clr_file->fi_inode : NULL, &lr);

	if (clr->cb.cbl_recall_type == RECALL_FILE)
		pnfs_return_file_layouts(clr->clr_client, clr->clr_file, &lr);
	else
	       pnfs_return_client_layouts(clr->clr_client, &lr,
		                          clr->cb.cbl_fsid.major);
}

/*
 * Recall a layout synchronously
 * must be called under the state lock
 */
static int
sync_layout_recall(struct super_block *sb, struct nfs4_layoutrecall *clr)
{
	struct nfs4_layoutrecall *pending;
	struct nfs4_client *clp = NULL;
	unsigned int i;
	int status;
	struct list_head todolist;
	static int (*__has_layout)(struct nfs4_client *,
				   struct nfs4_layoutrecall *);

	BUG_ON_UNLOCKED_STATE();
	INIT_LIST_HEAD(&todolist);

	/* specific client */
	if (clr->clr_client) {
		list_add(&clr->clr_perclnt, &todolist);
		clr = NULL;     /* so it won't be destroyed here */
		goto doit;
	}

	switch (clr->cb.cbl_recall_type) {
	case RECALL_FILE:
		__has_layout = cl_has_file_layout;
		break;
	case RECALL_FSID:
		__has_layout = cl_has_fsid_layout;
		break;
	case RECALL_ALL:
		__has_layout = cl_has_any_layout;
		break;
	}

	for (i = 0; i < CLIENT_HASH_SIZE; i++)
		list_for_each_entry(clp, &conf_str_hashtbl[i], cl_strhash)
			if (__has_layout(clp, clr)) {
				pending = alloc_init_layoutrecall(clr);
				if (!pending)
					goto doit;
				pending->clr_client = clp;
				list_add(&pending->clr_perclnt, &todolist);
			}
	/* cleanup only in the multi client, single client went into todolist */
	put_layoutrecall(clr);

doit:
	while (!list_empty(&todolist)) {
		pending = list_entry(todolist.next, struct nfs4_layoutrecall,
				     clr_perclnt);
		list_del_init(&pending->clr_perclnt);
		dprintk("%s: clp %p cb_client %p fp %p\n", __func__,
			pending->clr_client,
			pending->clr_client->cl_callback.cb_client,
			pending->clr_file);
		if (unlikely(!pending->clr_client->cl_callback.cb_client)) {
			printk(KERN_INFO
			       "%s: clientid %08x/%08x has no callback path\n",
			       __func__,
			       pending->clr_client->cl_clientid.cl_boot,
			       pending->clr_client->cl_clientid.cl_id);
			put_layoutrecall(pending);
			continue;
		}
		pending->clr_time = CURRENT_TIME;
		hash_layoutrecall(pending);

		status = nfsd4_cb_layout(pending);
		if (status) {
			/* if (status != NFSERR_NOMATCHING_LAYOUT) */
				printk("%s: clp %p cb_client %p fp %p "
				       "failed with status %d\n", __func__,
					pending->clr_client,
					pending->clr_client->cl_callback.cb_client,
					pending->clr_file,
					status);
			if (status == -NFSERR_NOMATCHING_LAYOUT)
				nomatching_layout(sb, pending);
			layoutrecall_done(pending);
		}

		put_layoutrecall(pending);
	}

	return 0;
}

/*
 * Spawn a thread to perform a recall layout
 *
 */
int nfsd_layout_recall_cb(struct super_block *sb, struct inode *inode,
			  struct nfsd4_pnfs_cb_layout *cbl)
{
	int status, did_lock;
	struct nfs4_layoutrecall *clr = NULL;

	dprintk("NFSD nfsd_layout_recall_cb: inode %p cbl %p\n", inode, cbl);
	BUG_ON(!cbl);
	BUG_ON(cbl->cbl_recall_type != RECALL_FILE &&
	       cbl->cbl_recall_type != RECALL_FSID &&
	       cbl->cbl_recall_type != RECALL_ALL);
	BUG_ON(cbl->cbl_recall_type == RECALL_FILE && !inode);
	BUG_ON(cbl->cbl_seg.iomode != IOMODE_READ &&
	       cbl->cbl_seg.iomode != IOMODE_RW &&
	       cbl->cbl_seg.iomode != IOMODE_ANY);

	if (nfsd_serv == NULL)
		return -ENOENT;

	clr = alloc_init_layoutrecall(NULL);
	if (!clr)
		return -ENOMEM;
	clr->cb = *cbl;

	INIT_LIST_HEAD(&clr->clr_perclnt);
	clr->clr_client = NULL;
	clr->clr_file = NULL;

	did_lock = nfs4_lock_state_nested();
	status = -ENOENT;
	if (clr->cb.cbl_seg.clientid) {
		clr->clr_client = find_confirmed_client(
			(clientid_t *)&clr->cb.cbl_seg.clientid);
		if (!clr->clr_client) {
			printk("%s: clientid %llx not found\n", __func__,
			       (unsigned long long)clr->cb.cbl_seg.clientid);
			goto err;
		}
	}
	if (inode) {
		clr->clr_file = find_file(inode);
		if (!clr->clr_file) {
			dprintk("NFSD nfsd_layout_recall_cb: "
				"nfs4_file not found\n");
			goto err;
		}
		if (cbl->cbl_recall_type == RECALL_FSID)
			clr->cb.cbl_fsid = clr->clr_file->fi_fsid;
	}

	status = sync_layout_recall(sb, clr);
	if (!status) {
		if (did_lock)
			nfs4_unlock_state();
		return 0;
	}

err:
	put_layoutrecall(clr);
	if (did_lock)
		nfs4_unlock_state();
	return status;
}

/*
 * Spawn a thread to perform a device notify
 *
 */
int nfsd_device_notify_cb(struct super_block *sb, struct nfsd4_pnfs_cb_device *nd)
{
	struct nfs4_notify_device cbnd;
	struct nfs4_client *clp = NULL;
	unsigned int i;
	int did_lock, status2, status = 0;

	dprintk("NFSD %s: cbl %p\n", __func__, nd);
	BUG_ON(!nd);
	BUG_ON(nd->cbd_notify_type != NOTIFY_DEVICEID4_CHANGE &&
	       nd->cbd_notify_type != NOTIFY_DEVICEID4_DELETE);

	if (nfsd_serv == NULL)
		return -ENOENT;

	did_lock = nfs4_lock_state_nested();

	cbnd.cbd = *nd;
	for (i = 0; i < CLIENT_HASH_SIZE; i++)
		list_for_each_entry(clp, &conf_str_hashtbl[i], cl_strhash) {
			cbnd.cbd_client = clp;
			status2 = nfsd4_cb_notify_device(&cbnd);
			if (status2)
				status = status2;
		}

	dprintk("NFSD %s: i %d status %d\n", __func__, i , status);
	if (did_lock)
		nfs4_unlock_state();
	return status;
}

#if defined(CONFIG_SPNFS)
int nfs4_spnfs_propagate_open(struct super_block *sb, struct svc_fh *current_fh,
				void *p)
{
	int status = 0;
	struct nfsd4_pnfs_open poa;
	struct nfsd4_open *openp = NULL;

	if (sb->s_export_op->propagate_open) {
		openp = (struct nfsd4_open *)p;
		poa.op_create = openp->op_create;
		poa.op_createmode = openp->op_createmode;
		poa.op_truncate = openp->op_truncate;
		strncpy(poa.op_fn, openp->op_fname.data, openp->op_fname.len);
		poa.op_fn[openp->op_fname.len] = '\0';

		status = sb->s_export_op->propagate_open(
			current_fh->fh_dentry->d_inode, &poa);
		if (status) {
			printk(KERN_WARNING
			"nfsd: pNFS could not be enabled for inode: %ld\n",
			current_fh->fh_dentry->d_inode->i_ino);
			/*
			* XXX When there's a failure then need to indicate to
			* future ops that no pNFS is available.  Should I
			* save the status in the inode?  It's kind of a big
			* hammer.  But there may be no stripes available?
			*/
		}
	}
	return status;
}
#endif /* CONFIG_SPNFS */

#endif /* CONFIG_PNFSD */
