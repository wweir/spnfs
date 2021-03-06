/*
 * linux/fs/nfs/callback.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback handling
 */

#include <linux/completion.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfs_fs.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#if defined(CONFIG_NFS_V4_1)
#include <linux/sunrpc/bc_xprt.h>
#endif

#include <net/inet_sock.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK

struct nfs_callback_data {
	unsigned int users;
	struct svc_serv *serv;
	pid_t pid;
	struct completion started;
	struct completion stopped;
};

static struct nfs_callback_data nfs_callback_info;
static DEFINE_MUTEX(nfs_callback_mutex);
static struct svc_program nfs4_callback_program;

unsigned int nfs_callback_set_tcpport;
unsigned short nfs_callback_tcpport;
static const int nfs_set_port_min = 0;
static const int nfs_set_port_max = 65535;

static int param_set_port(const char *val, struct kernel_param *kp)
{
	char *endp;
	int num = simple_strtol(val, &endp, 0);
	if (endp == val || *endp || num < nfs_set_port_min || num > nfs_set_port_max)
		return -EINVAL;
	*((int *)kp->arg) = num;
	return 0;
}

module_param_call(callback_tcpport, param_set_port, param_get_int,
		 &nfs_callback_set_tcpport, 0644);

/*
 * This is the callback kernel thread.
 */
static void nfs4_callback_svc(struct svc_rqst *rqstp)
{
	int err;

	__module_get(THIS_MODULE);
	lock_kernel();

	nfs_callback_info.pid = current->pid;
	daemonize("nfsv4-svc");
	/* Process request with signals blocked, but allow SIGKILL.  */
	allow_signal(SIGKILL);
	set_freezable();

	complete(&nfs_callback_info.started);

	for(;;) {
		if (signalled()) {
			if (nfs_callback_info.users == 0)
				break;
			flush_signals(current);
		}
		/*
		 * Listen for a request on the socket
		 */
		err = svc_recv(rqstp, MAX_SCHEDULE_TIMEOUT);
		if (err == -EAGAIN || err == -EINTR)
			continue;
		if (err < 0) {
			printk(KERN_WARNING
					"%s: terminating on error %d\n",
					__FUNCTION__, -err);
			break;
		}
		svc_process(rqstp);
	}

	flush_signals(current);
	svc_exit_thread(rqstp);
	nfs_callback_info.pid = 0;
	complete(&nfs_callback_info.stopped);
	unlock_kernel();
	module_put_and_exit(0);
}

#if defined(CONFIG_NFS_V4_1)

/*
 * The callback service for NFSv4.1 callbacks
 */
static void nfs41_callback_svc(struct svc_rqst *rqstp)
{
	struct svc_serv *serv = rqstp->rq_server;
	struct rpc_rqst *req;
	int error;
	DEFINE_WAIT(wq);

	__module_get(THIS_MODULE);

	lock_kernel();

	nfs_callback_info.pid = current->pid;
	daemonize("nfsv41-svc");
	/* Process request with signals blocked, but allow SIGKILL.  */
	allow_signal(SIGKILL);

	complete(&nfs_callback_info.started);

	for (;;) {
		if (signalled()) {
			if (nfs_callback_info.users == 0)
				break;
			flush_signals(current);
		}

		prepare_to_wait(&serv->sv_cb_waitq, &wq, TASK_INTERRUPTIBLE);
		spin_lock_bh(&serv->sv_cb_lock);
		if (!list_empty(&serv->sv_cb_list)) {
			req = list_first_entry(&serv->sv_cb_list,
					struct rpc_rqst, rq_bc_list);
			list_del(&req->rq_bc_list);
			spin_unlock_bh(&serv->sv_cb_lock);
			dprintk("Invoking bc_svc_process()\n");
			error = bc_svc_process(serv, req, rqstp);
			dprintk("bc_svc_process() returned w/ error code= %d\n",
				error);
		} else {
			spin_unlock_bh(&serv->sv_cb_lock);
			schedule();
		}
		finish_wait(&serv->sv_cb_waitq, &wq);
	}

	svc_exit_thread(rqstp);
	nfs_callback_info.pid = 0;
	complete(&nfs_callback_info.stopped);
	unlock_kernel();
	module_put_and_exit(0);
}
#endif /* CONFIG_NFS_V4_1 */


/*
 * Bring up the NFSv4 callback service
 */
int nfs4_callback_up(struct svc_serv *serv)
{
	int ret;

	ret = svc_create_xprt(serv, "tcp", nfs_callback_set_tcpport,
			      SVC_SOCK_ANONYMOUS);
	if (unlikely(ret <= 0)) {
		if (ret == 0)
			ret = -EIO;
		return ret;
	}
	nfs_callback_tcpport = ret;
	dprintk("Callback port = 0x%x\n", nfs_callback_tcpport);
	return svc_create_thread(nfs4_callback_svc, serv);
}

#if defined(CONFIG_NFS_V4_1)
/*
 * Bring up the NFSv4.1 callback service
 */
int nfs41_callback_up(struct svc_serv *serv, struct rpc_xprt *xprt)
{
	int ret = -ENOMEM;
	struct svc_xprt *bc_xprt;

	dprintk("--> %s\n", __func__);
	/* Create a svc_sock for the service */
	bc_xprt = svc_sock_create(serv, xprt->prot);
	if (!bc_xprt)
		goto out;

	/*
	 * Save the svc_serv in the transport so that it can
	 * be referenced when the session backchannel is initialized
	 */
	serv->bc_xprt = bc_xprt;
	xprt->bc_serv = serv;

	INIT_LIST_HEAD(&serv->sv_cb_list);
	spin_lock_init(&serv->sv_cb_lock);
	init_waitqueue_head(&serv->sv_cb_waitq);
	ret = svc_create_thread(nfs41_callback_svc, serv);
out:
	dprintk("--> %s return %d\n", __func__, ret);
	if (!ret)
		return 0;
	svc_sock_destroy(bc_xprt);
	return ret;
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Bring up the server process if it is not already up.
 */
int nfs_callback_up(int minorversion, void *args)
{
	struct svc_serv *serv = NULL;
	int ret = 0;
#if defined(CONFIG_NFS_V4_1)
	struct rpc_xprt *xprt = (struct rpc_xprt *)args;
#endif /* CONFIG_NFS_V4_1 */

	lock_kernel();
	mutex_lock(&nfs_callback_mutex);
	if (nfs_callback_info.users++ || nfs_callback_info.pid != 0) {
#if defined(CONFIG_NFS_V4_1)
		if (minorversion)
			xprt->bc_serv = nfs_callback_info.serv;
#endif /* CONFIG_NFS_V4_1 */
		goto out;
	}
	init_completion(&nfs_callback_info.started);
	init_completion(&nfs_callback_info.stopped);
	serv = svc_create(&nfs4_callback_program, NFS4_CALLBACK_BUFSIZE, NULL);
	ret = -ENOMEM;
	if (!serv)
		goto out_err;

	switch (minorversion) {
	case 0:
		ret = nfs4_callback_up(serv);
		break;
#if defined(CONFIG_NFS_V4_1)
	case 1:
		ret = nfs41_callback_up(serv, xprt);
		break;
#endif /* CONFIG_NFS_V4_1 */
	}
	if (ret < 0)
		goto out_err;
	nfs_callback_info.serv = serv;
	wait_for_completion(&nfs_callback_info.started);
out:
	/*
	 * svc_create creates the svc_serv with sv_nrthreads == 1, and then
	 * svc_create_thread increments that. So we need to call svc_destroy
	 * on both success and failure so that the refcount is 1 when the
	 * thread exits.
	 */
	if (serv)
		svc_destroy(serv);
	mutex_unlock(&nfs_callback_mutex);
	unlock_kernel();
	return ret;
out_err:
	dprintk("Couldn't create callback socket or server thread; err = %d\n",
		ret);
	nfs_callback_info.users--;
	goto out;
}

/*
 * Kill the server process if it is not already up.
 */
void nfs_callback_down(void)
{
	lock_kernel();
	mutex_lock(&nfs_callback_mutex);
	nfs_callback_info.users--;
	do {
		if (nfs_callback_info.users != 0 || nfs_callback_info.pid == 0)
			break;
		if (kill_proc(nfs_callback_info.pid, SIGKILL, 1) < 0)
			break;
	} while (wait_for_completion_timeout(&nfs_callback_info.stopped, 5*HZ) == 0);
	mutex_unlock(&nfs_callback_mutex);
	unlock_kernel();
}

static int nfs_callback_authenticate(struct svc_rqst *rqstp)
{
	struct nfs_client *clp;
	RPC_IFDEBUG(char buf[RPC_MAX_ADDRBUFLEN]);

	/* Don't talk to strangers */
	clp = nfs_find_client(svc_addr(rqstp), 4);
	if (clp == NULL)
		return SVC_DROP;

	dprintk("%s: %s NFSv4 callback!\n", __FUNCTION__,
			svc_print_addr(rqstp, buf, sizeof(buf)));
	nfs_put_client(clp);

	switch (rqstp->rq_authop->flavour) {
		case RPC_AUTH_NULL:
			if (rqstp->rq_proc != CB_NULL)
				return SVC_DENIED;
			break;
		case RPC_AUTH_UNIX:
			break;
		case RPC_AUTH_GSS:
			/* FIXME: RPCSEC_GSS handling? */
		default:
			return SVC_DENIED;
	}
	return SVC_OK;
}

/*
 * Define NFS4 callback program
 */
static struct svc_version *nfs4_callback_version[] = {
	[1] = &nfs4_callback_version1,
};

static struct svc_stat nfs4_callback_stats;

static struct svc_program nfs4_callback_program = {
	.pg_prog = NFS4_CALLBACK,			/* RPC service number */
	.pg_nvers = ARRAY_SIZE(nfs4_callback_version),	/* Number of entries */
	.pg_vers = nfs4_callback_version,		/* version table */
	.pg_name = "NFSv4 callback",			/* service name */
	.pg_class = "nfs",				/* authentication class */
	.pg_stats = &nfs4_callback_stats,
	.pg_authenticate = nfs_callback_authenticate,
};
