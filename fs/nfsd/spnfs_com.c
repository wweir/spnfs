/*
 * fs/nfsd/spnfs_com.c
 *
 * Communcation layer between spNFS kernel and userspace
 * Based heavily on idmap.c
 *
 */

/*
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
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
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>

#include <linux/sunrpc/clnt.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#define	NFSDDBG_FACILITY		NFSDDBG_PROC

static ssize_t   spnfs_pipe_upcall(struct file *, struct rpc_pipe_msg *,
		     char __user *, size_t);
static ssize_t   spnfs_pipe_downcall(struct file *, const char __user *,
		     size_t);
static void      spnfs_pipe_destroy_msg(struct rpc_pipe_msg *);

/* RPC pipefs upcall/downcall routines */
/* looks like this code is invoked by the rpc_pipe code */
/* to handle upcalls on things we've queued elsewhere */
/* See nfs_idmap_id for an exmaple of enqueueing */
static ssize_t
spnfs_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
    char __user *dst, size_t buflen)
{
	char *data = (char *)msg->data + msg->copied;
	ssize_t mlen = msg->len - msg->copied;
	ssize_t left;

	if (mlen > buflen)
		mlen = buflen;

	left = copy_to_user(dst, data, mlen);
	if (left < 0) {
		msg->errno = left;
		return left;
	}
	mlen -= left;
	msg->copied += mlen;
	msg->errno = 0;
	return mlen;
}

static ssize_t
spnfs_pipe_downcall(struct file *filp, const char __user *src, size_t mlen)
{
	struct rpc_inode *rpci = RPC_I(filp->f_dentry->d_inode);
	struct spnfs *spnfs = (struct spnfs *)rpci->private;
	struct spnfs_msg im_in, *im = &spnfs->spnfs_im;
	int ret;

	if (mlen != sizeof(im_in))
		return (-ENOSPC);

	if (copy_from_user(&im_in, src, mlen) != 0)
		return (-EFAULT);

	mutex_lock(&spnfs->spnfs_plock);

	ret = mlen;
	im->im_status = im_in.im_status;
	/* If we got an error, terminate now, and wake up pending upcalls */
	if (!(im_in.im_status & SPNFS_STATUS_SUCCESS)) {
		wake_up(&spnfs->spnfs_wq);
		goto out;
	}

	ret = -EINVAL;
	/* Did we match the current upcall? */
	/* DMXXX: do not understand the comment above, from original code */
	/* DMXXX: when do we _not_ match the current upcall? */
	/* DMXXX: anyway, let's to a simplistic check */
	if (im_in.im_type == im->im_type) {
		/* copy the response into the spnfs struct */
		memcpy(&im->im_res, &im_in.im_res, sizeof(im->im_res));
		ret = mlen;
	} else
		dprintk("spnfs: downcall type != upcall type\n");


	wake_up(&spnfs->spnfs_wq);
/* DMXXX handle rval processing */
out:
	mutex_unlock(&spnfs->spnfs_plock);
	return ret;
}

static void
spnfs_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct spnfs_msg *im = msg->data;
	struct spnfs *spnfs = container_of(im, struct spnfs, spnfs_im);

	if (msg->errno >= 0)
		return;
	mutex_lock(&spnfs->spnfs_plock);
	im->im_status = SPNFS_STATUS_FAIL;  /* DMXXX */
	wake_up(&spnfs->spnfs_wq);
	mutex_unlock(&spnfs->spnfs_plock);
}
