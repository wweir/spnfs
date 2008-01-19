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

/*
 * The NFSv4.1 callback service helper routines.
 * They implement the transport level processing required to send the
 * reply over an existing open connection previously established by the client.
 */

#if defined(CONFIG_NFS_V4_1)

#include <linux/module.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/bc_xprt.h>

#define RPCDBG_FACILITY	RPCDBG_SVCDSP

/*
 * Free an existing backchannel rpc task
 */
static void bc_release_task(struct rpc_task *task)
{
	rpc_put_task(task);
}

/*
 * Reserve the xprt
 */
static int bc_reserve_xprt(struct rpc_task *task)
{
	int err = 0;
	struct rpc_xprt *xprt = task->tk_rqstp->rq_xprt;

	BUG_ON(xprt == NULL);
	dprintk("RPC:       bc_reserve_xprt: task= %p, xprt= %p\n", task, xprt);
	spin_lock_bh(&xprt->transport_lock);
	if (!xprt->ops->reserve_xprt(task)) {
		err = -EAGAIN;
		goto out_unlock;
	}

	if (!xprt_connected(xprt)) {
		err = -ENOTCONN;
		goto out_unlock;
	}
out_unlock:
	spin_unlock_bh(&xprt->transport_lock);
	dprintk("RPC:       bc_reserve_xprt err= %d \n", err);
	return err;
}

/*
 * Release the xprt
 */
static void bc_release_xprt(struct rpc_task *task)
{
	dprintk("RPC:       bc_release_xprt: task= %p\n", task);
	xprt_end_transmit(task);
}

/* Empty callback ops */
static const struct rpc_call_ops nfs41_callback_ops = {
};

/*
 * Send the callback reply
 */
int bc_send(struct rpc_rqst *req)
{
	struct rpc_task *bc_task;
	int err = -ENOMEM;
	struct xdr_buf *xbufp = &req->rq_snd_buf;
	struct rpc_xprt *xprt = req->rq_xprt;

	dprintk("RPC:       bc_send req= %p\n", req);
	/*
	 * Create an rpc_task to send the data
	 */
	bc_task = rpc_new_bc_task(req, 0, &nfs41_callback_ops, NULL);
	if (!bc_task)
		goto out_no_free;

	/*
	 * Reserve the xprt and then try to send the request across
	 */
	err = bc_reserve_xprt(bc_task);
	if (err)
		goto out;

	/*
	 * Set up the xdr_buf length
	 */
	xbufp->len = xbufp->head[0].iov_len + xbufp->page_len +
			xbufp->tail[0].iov_len;

	err = xprt->ops->send_request(bc_task);

	bc_release_xprt(bc_task);

out:
	bc_task->tk_rqstp = NULL;
	bc_release_task(bc_task);

out_no_free:
	xprt_free_bc_request(req);
	dprintk("RPC:       bc_send: err= %d\n", err);
	return err;
}
EXPORT_SYMBOL_GPL(bc_send);

#endif /* CONFIG_NFS_V4_1 */
