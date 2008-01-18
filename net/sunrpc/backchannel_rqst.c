/******************************************************************************

(c) 2007 Network Appliance, Inc.  All Rights Reserved.

Network Appliance provides this source code under the GPL v2 License.
The GPL v2 license is available at
http://opensource.org/licenses/gpl-license.php.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

#include <linux/tcp.h>
#include <linux/sunrpc/xprt.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#if defined(CONFIG_NFS_V4_1)

int xprt_setup_backchannel(struct rpc_xprt *xprt, unsigned int min_reqs)
{
	struct page *page_priv = NULL, *page_snd = NULL;
	struct xdr_buf *xbufp = NULL;
	struct rpc_rqst *rqstp;

	BUG_ON(min_reqs > 1);	/* We only prealloate buffers for one slot */
	dprintk("RPC:       setup backchannel transport\n");

	/* Pre-allocate one backchannel rpc_rqst */
	rqstp = kmalloc(sizeof(struct rpc_rqst), GFP_KERNEL);
	if (rqstp == NULL) {
		printk(KERN_ERR "Failed to create backchannel rpc_rqst\n");
		goto out_free;
	}
	rqstp->rq_xprt = xprt;

	/* Preallocate one XDR private buffer */
	page_priv = alloc_page(GFP_KERNEL);
	if (page_priv == NULL) {
		printk(KERN_ERR "Failed to create backchannel priv xbuf\n");
		goto out_free;
	}
	xbufp = &rqstp->rq_private_buf;
	xbufp->head[0].iov_base = page_address(page_priv);
	xbufp->head[0].iov_len = PAGE_SIZE;
	xbufp->tail[0].iov_base = NULL;
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

	xbufp = &rqstp->rq_snd_buf;
	xbufp->head[0].iov_base = page_address(page_snd);
	xbufp->head[0].iov_len = 0;
	xbufp->tail[0].iov_base = NULL;
	xbufp->tail[0].iov_len = 0;
	xbufp->page_len = 0;
	xbufp->len = 0;
	xbufp->buflen = PAGE_SIZE;

	clear_bit(RPC_BC_PA_IN_USE, &rqstp->rq_bc_pa_state);

	/* Add the allocated buffer to the preallocation list */
	write_lock(&xprt->bc_pa_lock);
	list_add(&rqstp->rq_bc_pa_list, &xprt->bc_pa_list);
	write_unlock(&xprt->bc_pa_lock);

	dprintk("RPC:       added rqstp= %p\n", rqstp);
	dprintk("RPC:       setup backchannel transport done\n");
	return 0;

out_free:
	kfree(page_snd);
	kfree(page_priv);
	kfree(rqstp);

	dprintk("RPC:       setup backchannel transport failed\n");
	return -1;
}
EXPORT_SYMBOL(xprt_setup_backchannel);

void xprt_destroy_backchannel(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req = NULL, *tmp = NULL;
	struct xdr_buf *xbufp;

	dprintk("RPC:        destroy backchannel transport\n");
	write_lock(&xprt->bc_pa_lock);
	list_for_each_entry_safe_continue(req, tmp, &xprt->bc_pa_list,
					  rq_bc_pa_list) {
		dprintk("RPC:        req=%p\n", req);
		BUG_ON(req->rq_bc_pa_state & RPC_BC_PA_IN_USE);
		xbufp = &req->rq_private_buf;
		kfree(xbufp->head[0].iov_base);
		xbufp = &req->rq_snd_buf;
		kfree(xbufp->head[0].iov_base);
		list_del(&req->rq_bc_pa_list);
		kfree(req);
	}
	write_unlock(&xprt->bc_pa_lock);
	dprintk("RPC:        destroy backchannel transport done\n");
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

	dprintk("RPC:       allocate a backchannel request\n");
	read_lock(&xprt->bc_pa_lock);
	list_for_each_entry(req, &xprt->bc_pa_list, rq_bc_pa_list) {
		dprintk("RPC:       is req=%p in use?\n", req);
		if (!test_and_set_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state)) {
			dprintk("RPC:       req=%p is not in use\n", req);
			goto found;		/* found */
		}
		dprintk("RPC:       req=%p is in use\n", req);
	}
	req = NULL;

found:	read_unlock(&xprt->bc_pa_lock);
	dprintk("RPC:       backchannel req=%p\n", req);
	return req;
}

/*
 * Return the preallocated rpc_rqst structure and XDR buffers
 */
void xprt_free_bc_request(struct rpc_rqst *req)
{
	dprintk("RPC:       free backchannel req=%p\n", req);
	smp_mb__before_clear_bit();
	BUG_ON(!test_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state));
	clear_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state);
	smp_mb__after_clear_bit();

}
EXPORT_SYMBOL(xprt_free_bc_request);

#endif /* CONFIG_NFS_V4_1 */
