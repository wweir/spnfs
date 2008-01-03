/*
 * This file is distributed under GPL. Please see Documentation/COPYING for
 * details.
 *
 * Author: Rahul Iyer <iyer@netapp.com>
 * 	   Ricardo Labiaga <ricardo.labiaga@netapp.com>
 */

#include <linux/tcp.h>
#include <linux/sunrpc/xprt.h>

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#if defined(CONFIG_NFS_V4_1)

int xprt_setup_backchannel(struct rpc_xprt *xprt, unsigned int min_reqs)
{
	struct page *page_priv = NULL, *page_snd = NULL;
	struct xdr_buf *xbufp = NULL;

	BUG_ON(min_reqs > 1);	/* We only prealloate buffers for one slot */
	dprintk("RPC:        setup backchannel transport\n");

	/* Pre-allocate one backchannel rpc_rqst */
	xprt->bc_rpc_rqst = kmalloc(sizeof(struct rpc_rqst), GFP_KERNEL);
	if (xprt->bc_rpc_rqst == NULL) {
		printk(KERN_ERR "Failed to create backchannel rpc_rqst\n");
		goto out_free;
	}
	xprt->bc_rpc_rqst->rq_xprt = xprt;

	/* Preallocate one XDR private buffer */
	page_priv = alloc_page(GFP_KERNEL);
	if (page_priv == NULL) {
		printk(KERN_ERR "Failed to create backchannel priv xbuf\n");
		goto out_free;
	}
	xbufp = &xprt->bc_rpc_rqst->rq_private_buf;
	xbufp->head[0].iov_base = page_address(page_priv);
	xbufp->head[0].iov_len = PAGE_SIZE;
	xbufp->tail[0].iov_len = 0;
	xbufp->page_len = 0;
	xbufp->len = PAGE_SIZE;
	xbufp->buflen = PAGE_SIZE;

	/* Preallocate one XDR send buffer */
	page_snd = alloc_page(GFP_KERNEL);
	if (page_snd == NULL) {
		printk(KERN_ERR "Failed to create backchannel snd xbuf\n");
		goto out_free;
	}

	xbufp = &xprt->bc_rpc_rqst->rq_snd_buf;
	xbufp->head[0].iov_base = page_address(page_snd);
	xbufp->head[0].iov_len = 0;
	xbufp->tail[0].iov_len = 0;
	xbufp->page_len = 0;
	xbufp->len = 0;
	xbufp->buflen = PAGE_SIZE;

	dprintk("RPC:        setup backchannel transport done\n");
	return 0;

out_free:
	kfree(page_snd);
	kfree(page_priv);
	kfree(xprt->bc_rpc_rqst);

	dprintk("RPC:        setup backchannel transport failed\n");
	return -1;
}
EXPORT_SYMBOL(xprt_setup_backchannel);

void xprt_destroy_backchannel(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req = xprt->bc_rpc_rqst;
	struct xdr_buf *xbufp;

	dprintk("RPC:        destroy backchannel transport\n");
	/*
	 * Any of these can be NULL if the user hit ^C
	 */
	if (req == NULL)
		return;		/* Nothing to do */
	xbufp = &req->rq_private_buf;
	if (xbufp)
		kfree(xbufp->head[0].iov_base);
	xbufp = &req->rq_snd_buf;
	if (xbufp)
		kfree(xbufp->head[0].iov_base);
	kfree(xprt->bc_rpc_rqst);
}
EXPORT_SYMBOL(xprt_destroy_backchannel);

/*
 * A single rpc_rqst structure has been preallocated during the backchannel
 * setup.  Buffer space for the send and private XDR buffers has been
 * preallocated as well.  Use xprt_alloc_bc_request to allocate this
 * space for this request.  Use xprt_free_bc_request to return it.
 *
 * Returns the rpc_rqst if it's not in use, otherwise NULL.
 */
struct rpc_rqst *xprt_alloc_bc_request(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req;

	dprintk("RPC:        allocate a backchannel request\n");
	req = test_and_set_bit(RPC_BC_PREALLOC_IN_USE, &xprt->bc_flags) ?
		NULL : xprt->bc_rpc_rqst;

	dprintk("RPC:        backchannel req=%p\n", req);
	return req;
}

/*
 * Return the preallocated rpc_rqst structure and XDR buffers
 */
void xprt_free_bc_request(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	dprintk("RPC:        free backchannel req=%p\n", req);

	smp_mb__before_clear_bit();
	clear_bit(RPC_BC_PREALLOC_IN_USE, &xprt->bc_flags);
	smp_mb__after_clear_bit();

}
EXPORT_SYMBOL(xprt_free_bc_request);

#endif /* CONFIG_NFS_V4_1 */
