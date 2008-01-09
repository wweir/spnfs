/*
 * NFSv4.1 session recovery code
 *
 * Author: Rahul Iyer <iyer@netapp.com>
 *
 * This code is released under GPL. For details see Documentation/COPYING
 */

#if defined(CONFIG_NFS_V4_1)

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs3.h>
#include <linux/nfs_xdr.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/namei.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs41_session_recovery.h>
#include "nfs4_fs.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

/*
 * Session state bits
 */
enum nfs41_session_state {
	NFS41_SESSION_EXPIRED = 0,
	NFS41_SESSION_RECOVER,
};

/*
 * Set the session state expired
 */
int nfs41_set_session_expired(struct nfs4_session *session)
{
	set_bit(NFS41_SESSION_EXPIRED, &session->session_state);

	return 0;
}

int nfs41_test_session_expired(struct nfs4_session *session)
{
	return test_bit(NFS41_SESSION_EXPIRED, &session->session_state);
}

/*
 * Set the session state == valid. Returns previous value of the session state
 */
int nfs41_set_session_valid(struct nfs4_session *session)
{
	int ret;
	smp_mb__before_clear_bit();
	ret = test_and_clear_bit(NFS41_SESSION_EXPIRED,
				&session->session_state);
	smp_mb__after_clear_bit();

	return ret;
}

static int nfs41_start_session_recovery(struct nfs4_session *session)
{
	int ret;
	ret = test_and_set_bit(NFS41_SESSION_RECOVER, &session->session_state);

	return ret;
}

struct reclaimer_arg {
	struct nfs_client *clp;
	struct nfs4_session *session;
};

static int nfs41_end_session_recovery(struct reclaimer_arg *rec)
{
	smp_mb__before_clear_bit();
	clear_bit(NFS41_SESSION_RECOVER, &rec->session->session_state);
	smp_mb__after_clear_bit();

	/*
	 * Wake up async tasks
	 */
	rpc_wake_up(&rec->session->recovery_waitq);

	/*
	 * Wake up sync tasks
	 */
	wake_up_bit(&rec->session->session_state, NFS41_SESSION_RECOVER);

	kfree(rec);
	return 0;
}

static int nfs41_recovery_complete(struct nfs4_session *session)
{
	return (!test_bit(NFS41_SESSION_RECOVER, &session->session_state));
}

extern int nfs4_wait_bit_interruptible(void *word);
static int nfs41_wait_session_recover_sync(struct rpc_clnt *clnt,
					   struct nfs4_session *session)
{
	sigset_t oldset;
	int ret;

	might_sleep();

	rpc_clnt_sigmask(clnt, &oldset);
	ret = wait_on_bit(&session->session_state, NFS41_SESSION_RECOVER,
				nfs4_wait_bit_interruptible,
				TASK_INTERRUPTIBLE);
	rpc_clnt_sigunmask(clnt, &oldset);

	return ret;
}

static int nfs41_wait_session_recover_async(struct rpc_task *task,
					    struct nfs4_session *session)
{
	if (nfs41_recovery_complete(session)) {
		rpc_wake_up_task(task);
		return 0;
	}

	return -EAGAIN;
}

int nfs4_proc_create_session(struct nfs_client *clp,
			     struct nfs4_session *session);

static int session_reclaimer(void *arg)
{
	int ret;
	struct reclaimer_arg *rec = (struct reclaimer_arg *)arg;

	allow_signal(SIGKILL);

	ret = nfs4_proc_create_session(rec->clp, rec->session);
	if (ret)
		goto out_error;

out:
	nfs41_end_session_recovery(rec);
	module_put_and_exit(0);
	return ret;
out_error:
	printk(KERN_WARNING "Error: session recovery failed on "
		"NFSv4.1 server %u.%u.%u.%u with error %d\n",
		NIPQUAD(rec->clp->cl_addr.sin_addr),
		-ret);
	nfs41_set_session_expired(rec->session);

	switch (ret) {
	case -NFS4ERR_STALE_CLIENTID:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_EXPIRED:
		set_bit(NFS4CLNT_LEASE_EXPIRED, &rec->clp->cl_state);
		break;
	}
	goto out;
}

static int nfs41_schedule_session_recovery(struct reclaimer_arg *rec)
{
	struct task_struct *task;

	dprintk("--> %s: spawning session_reclaimer\n", __func__);
	__module_get(THIS_MODULE);
	task = kthread_run(session_reclaimer, rec, "%llx-session-reclaim",
				(u64 *)rec->session->sess_id);

	if (!IS_ERR(task)) {
		dprintk("<-- %s\n", __func__);
		return 0;
	}

	module_put(THIS_MODULE);
	dprintk("--> %s: failed spawning session_reclaimer: error=%ld\n",
		__func__, PTR_ERR(task));
	return PTR_ERR(task);
}

/*
 * Session recovery
 * Called when an op receives a session related error
 */
int nfs41_recover_session(struct nfs_client *clp, struct nfs4_session *session)
{
	struct reclaimer_arg *rec;
	int ret;

	printk(KERN_INFO "--> %s clp %p session %p\n", __func__, clp, session);
	/* freed in nfs41_end_session_recovery */
	rec = kmalloc(sizeof(*rec), GFP_KERNEL);
	if (!rec)
		return -ENOMEM;
	rec->clp = clp;
	rec->session = session;

	printk(KERN_INFO "%s rec %p\n", __func__, rec);
	ret = nfs41_start_session_recovery(session);

	/*
	 * If we get 1, it means some other thread beat us to us here, so we
	 * just sit back and wait for completion of the recovery process
	 */
	if (ret)
		return 0;

	ret = nfs41_schedule_session_recovery(rec);
	if (!ret)
		goto out;
	/*
	 * We got an error creating the reclaiming thread, so end the recovery
	 * and bail out
	 */
	nfs41_end_session_recovery(rec);
out:
	return ret;
}

int nfs41_recover_session_sync(struct rpc_clnt *clnt, struct nfs_client *clp,
				struct nfs4_session *session)
{
	int ret;

	ret = nfs41_recover_session(clp, session);
	if (ret)
		return ret;

	return nfs41_wait_session_recover_sync(clnt, session);
}
EXPORT_SYMBOL(nfs41_recover_session_sync);

int nfs41_recover_session_async(struct rpc_task *task,
				struct nfs_server *server)
{
	int ret;

	rpc_sleep_on(&server->session->recovery_waitq, task, NULL, NULL);
	ret = nfs41_recover_session(server->nfs_client, server->session);

	ret = nfs41_wait_session_recover_async(task, server->session);

	return ret;
}

int nfs41_recover_expired_session1(struct rpc_clnt *clnt,
				  struct nfs_client *clp,
				  struct nfs4_session *session)
{
	int ret;

	while (1) {
		ret = nfs41_wait_session_recover_sync(clnt, session);
		if (ret)
			return ret;

		if (!nfs41_set_session_valid(session))
			break;
		ret = nfs41_recover_session_sync(clnt, clp, session);
	}

	return ret;
}

int nfs41_recover_expired_session(struct rpc_task *task,
				  struct nfs_client *clp,
				  struct nfs4_session *session)
{
	int ret = 0;

	dprintk("--> %s\n", __func__);
	while (1) {
		rpc_sleep_on(&session->recovery_waitq, task, NULL, NULL);

		ret = nfs41_wait_session_recover_async(task, session);
		if (ret == -EAGAIN)
			break;
		ret = nfs41_set_session_valid(session);
		if (!ret)
			break;

		nfs41_recover_session(clp, session);
	}

	dprintk("<-- %s: status=%d\n", __func__, ret);
	return ret;
}

#endif /* CONFIG_NFS_V4_1 */
