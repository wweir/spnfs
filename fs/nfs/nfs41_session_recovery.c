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

static int nfs41_end_session_recovery(struct nfs4_session *session)
{
	smp_mb__before_clear_bit();
	clear_bit(NFS41_SESSION_RECOVER, &session->session_state);
	smp_mb__after_clear_bit();

	/*
	 * Wake up sync tasks
	 */
	wake_up_bit(&session->session_state, NFS41_SESSION_RECOVER);
	return 0;
}

static int nfs41_wait_session_recover_sync(struct rpc_clnt *clnt,
					   struct nfs4_session *session)
{
	might_sleep();
	return wait_on_bit(&session->session_state, NFS41_SESSION_RECOVER,
			   nfs4_wait_bit_killable, TASK_KILLABLE);
}

int nfs4_proc_create_session(struct nfs_client *clp,
			     struct nfs4_session *session);

static int session_reclaimer(void *arg)
{
	int ret;
	struct reclaimer_arg *rec = (struct reclaimer_arg *)arg;

	dprintk("--> %s\n", __func__);
	allow_signal(SIGKILL);

	ret = nfs4_proc_create_session(rec->clp, rec->session);
	if (ret)
		goto out_error;

out:
	nfs41_end_session_recovery(rec->session);
	kfree(rec);
	module_put_and_exit(0);
	dprintk("<-- %s: status=%d\n", __func__, ret);
	return ret;
out_error:
	printk(KERN_WARNING "Error: session recovery failed on "
		"NFSv4.1 server with error %d\n", ret);
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
	struct reclaimer_arg *rec = NULL;
	int ret;

	dprintk("--> %s: clp=%p session=%p\n", __func__, clp, session);

	ret = nfs41_start_session_recovery(session);

	/*
	 * If we get 1, it means some other thread beat us to us here, so we
	 * just sit back and wait for completion of the recovery process
	 */
	if (ret) {
		dprintk("%s: session_recovery already started\n", __func__);
		ret = 0;
		goto out;
	}

	ret = -ENOMEM;
	rec = kmalloc(sizeof(*rec), GFP_KERNEL);
	if (!rec)
		goto err;
	rec->clp = clp;
	rec->session = session;

	ret = nfs41_schedule_session_recovery(rec);
	/*
	 * We got an error creating the reclaiming thread, so end the recovery
	 * and bail out
	 */
	if (ret)
		goto err;
out:
	dprintk("<-- %s status=%d\n", __func__, ret);
	return ret;
err:
	nfs41_end_session_recovery(session);
	kfree(rec);
	goto out;
}

int nfs41_recover_session_sync(struct rpc_clnt *clnt, struct nfs_client *clp,
				struct nfs4_session *session)
{
	int ret;

	dprintk("--> %s\n", __func__);

	ret = nfs41_recover_session(clp, session);
	if (!ret)
		ret = nfs41_wait_session_recover_sync(clnt, session);

	dprintk("<-- %s: status=%d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(nfs41_recover_session_sync);

#endif /* CONFIG_NFS_V4_1 */
