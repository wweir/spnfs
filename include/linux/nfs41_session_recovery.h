/*
 * Session Recovery header file
 *
 * Author: Rahul Iyer <iyer@netapp.com>
 *
 * This code is released under GPL. For details see Documentation/COPYING
 */

#ifndef __NFS41_SESSION_RECOVERY_H__
#define __NFS41_SESSION_RECOVERY_H__

#if defined(CONFIG_NFS_V4_1)

int nfs41_set_session_expired(struct nfs4_session *session);
int nfs41_set_session_valid(struct nfs4_session *session);
int nfs41_recover_session(struct nfs_server *server);
int nfs41_recover_session_sync(struct rpc_clnt *, struct nfs_server *server);
int nfs41_recover_session_async(struct rpc_task *, struct nfs_server *server);
int nfs41_recover_expired_session(struct rpc_task *task, struct nfs_server
*server);
int nfs41_test_session_expired(struct nfs4_session *session);

#endif	/* CONFIG_NFS_V4_1 */
#endif	/* __NFS41_SESSION_RECOVERY_H__ */

