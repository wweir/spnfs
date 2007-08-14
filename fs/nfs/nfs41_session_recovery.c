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
#include <linux/namei.h>
#include <linux/nfs_fs_sb.h>
#include "nfs4_fs.h"

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

static int nfs41_end_session_recovery(struct nfs4_session *session)
{
	smp_mb__before_clear_bit();
	clear_bit(NFS41_SESSION_RECOVER, &session->session_state);
	smp_mb__after_clear_bit();

	/*
	 * Wake up async tasks
	 */
	rpc_wake_up(&session->recovery_waitq);

	/*
	 * Wake up sync tasks
	 */
	wake_up_bit(&session->session_state, NFS41_SESSION_RECOVER);

	return 0;
}

static int nfs41_recovery_complete(struct nfs4_session *session)
{
	return (!test_bit(NFS41_SESSION_RECOVER, &session->session_state));
}

extern int nfs4_wait_bit_interruptible(void *word);
static int nfs41_wait_session_recover_sync(struct rpc_clnt *clnt,
					   struct nfs_server *server)
{
	sigset_t oldset;
	int ret;
	struct nfs4_session *session = server->session;

	might_sleep();

	rpc_clnt_sigmask(clnt, &oldset);
	ret = wait_on_bit(&session->session_state, NFS41_SESSION_RECOVER,
				nfs4_wait_bit_interruptible,
				TASK_INTERRUPTIBLE);
	rpc_clnt_sigunmask(clnt, &oldset);

	return ret;
}

static int nfs41_wait_session_recover_async(struct rpc_task *task,
					    struct nfs_server *server)
{
	if (nfs41_recovery_complete(server->session))
		rpc_wake_up_task(task);

	return 0;
}

int nfs4_proc_create_session(struct nfs_server *sp);

static int session_reclaimer(void *arg)
{
	int ret;
	struct nfs_server *server = (struct nfs_server *)arg;
	struct nfs4_session *session = server->session;

	allow_signal(SIGKILL);

	ret = nfs4_proc_create_session(server);
	if (ret)
		goto out_error;

out:
	nfs41_end_session_recovery(session);
	module_put_and_exit(0);
	return ret;
out_error:
	printk(KERN_WARNING "Error: session recovery failed on "
		"NFSv4.1 server %u.%u.%u.%u with error %d\n",
		NIPQUAD(server->nfs_client->cl_addr.sin_addr),
		-ret);
	nfs41_set_session_expired(server->session);
	goto out;
}

static int nfs41_schedule_session_recovery(struct nfs_server *server)
{
	struct task_struct *task;

	__module_get(THIS_MODULE);
	task = kthread_run(session_reclaimer, server, "%llx-session-reclaim",
	(u64 *)server->session->sess_id);

	if (!IS_ERR(task))
		return 0;

	module_put(THIS_MODULE);

	return PTR_ERR(task);
}

/*
 * Session recovery
 * Called when an op receives a session related error
 */
int nfs41_recover_session(struct nfs_server *server)
{
	int ret;

	ret = nfs41_start_session_recovery(server->session);

	/*
	 * If we get 1, it means some other thread beat us to us here, so we
	 * just sit back and wait for completion of the recovery process
	 */
	if (ret)
		return 0;

	ret = nfs41_schedule_session_recovery(server);
	if (!ret)
		goto out;
	/*
	 * We got an error creating the reclaiming thread, so end the recovery
	 * and bail out
	 */
	nfs41_end_session_recovery(server->session);
out:
	return ret;
}

int nfs41_recover_session_sync(struct rpc_clnt *clnt, struct nfs_server *server)
{
	int ret;

	ret = nfs41_recover_session(server);
	if (ret)
		return ret;

	return nfs41_wait_session_recover_sync(clnt, server);
}

int nfs41_recover_session_async(struct rpc_task *task,
				struct nfs_server *server)
{
	int ret;

	rpc_sleep_on(&server->session->recovery_waitq, task, NULL, NULL);
	ret = nfs41_recover_session(server);

	nfs41_wait_session_recover_async(task, server);

	return ret;
}

int nfs41_recover_expired_session(struct rpc_clnt *clnt,
				  struct nfs_server *server)
{
	int ret;

	while (1) {
		ret = nfs41_wait_session_recover_sync(clnt, server);
		if (ret)
			return ret;

		if (!nfs41_set_session_valid(server->session))
			break;

		ret = nfs41_recover_session_sync(clnt, server);
	}

	return ret;
}

#endif /* CONFIG_NFS_V4_1 */
