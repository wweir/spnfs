/*
 * This file is Distributed under GPL. See Documentation/COPYING.
 *
 * Author: Rahul Iyer <iyer@netapp.com>
 *
 * Functions to create and manage the backchannel
 */

#ifndef _LINUX_SUNRPC_BC_XPRT_H
#define _LINUX_SUNRPC_BC_XPRT_H

#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/xprt.h>

#ifdef CONFIG_NFS_V4_1
struct rpc_rqst *xprt_alloc_bc_request(struct rpc_xprt *xprt);
void xprt_free_bc_request(struct rpc_rqst *req);
int xprt_setup_backchannel(struct rpc_xprt *, unsigned int min_reqs);
void xprt_destroy_backchannel(struct rpc_xprt *);

struct rpc_task *rpc_alloc_task(void);
void rpc_free_task(struct rpc_task *task);
#endif /* CONFIG_NFS_V4_1 */
#endif

