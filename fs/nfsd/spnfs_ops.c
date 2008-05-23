/*
 * fs/nfsd/spnfs_ops.c
 *
 * Communcation layer between spNFS kernel and userspace
 *
 */
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

#if defined(CONFIG_SPNFS)
/* DMXXX: includes taken from spnfs_com.c.  Don't need so many: revisit */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>

#include <linux/sunrpc/clnt.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#include <linux/nfs_fs.h>
#include <linux/nfs4.h>
#include <linux/exportfs.h>
#include <linux/nfsd4_spnfs.h>
#include <linux/nfsd/debug.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/nfsd4_pnfs.h>
#include <linux/nfsd/nfs4layoutxdr.h>

#define	NFSDDBG_FACILITY		NFSDDBG_PNFS

/*
 * The functions that are called from elsewhere in the kernel
 * to perform tasks in userspace
 *
 */

extern struct spnfs *global_spnfs;
static struct inode *recall_inode;

int
spnfs_layout_type(void)
{
	return LAYOUT_NFSV4_FILES;
}

int
spnfs_layoutget(struct inode *inode, struct pnfs_layoutget_arg *lgp)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	struct pnfs_filelayout_layout *flp = NULL;
	int status = 0, i;

	im.im_type = SPNFS_TYPE_LAYOUTGET;
	im.im_args.layoutget_args.inode = inode->i_ino;

	/* call function to queue the msg for upcall */
	if (spnfs_upcall(spnfs, &im, &res) != 0) {
		dprintk("failed spnfs upcall: layoutget\n");
		status = -EIO;
		goto layoutget_cleanup;
	}
	status = res.layoutget_res.status;
	if (status != 0)
		goto layoutget_cleanup;

	lgp->return_on_close = 0;
	lgp->seg.length = NFS4_LENGTH_EOF;

	flp = kmalloc(sizeof(struct pnfs_filelayout_layout), GFP_KERNEL);
	if (flp == NULL) {
		status = -ENOMEM;
		goto layoutget_cleanup;
	}
	flp->device_id.pnfs_fsid = lgp->fsid;
	flp->device_id.pnfs_devid = res.layoutget_res.devid;
	flp->lg_layout_type = 1; /* XXX */
	flp->lg_stripe_type = res.layoutget_res.stripe_type;
	flp->lg_commit_through_mds = 0;
	flp->lg_stripe_unit =  res.layoutget_res.stripe_size;
	flp->lg_first_stripe_index = 0;
	flp->lg_pattern_offset = 0;
	flp->lg_fh_length = res.layoutget_res.stripe_count;

	flp->lg_fh_list = kmalloc(flp->lg_fh_length * sizeof(struct knfsd_fh),
				  GFP_KERNEL);
	if (flp->lg_fh_list == NULL) {
		status = -ENOMEM;
		goto layoutget_cleanup;
	}
	/*
	 * FIX: Doing an extra copy here.  Should group res.flist's fh_len
	 * and fh_val into a knfsd_fh structure.
	 */
	for (i = 0; i < flp->lg_fh_length; i++) {
		flp->lg_fh_list[i].fh_size = res.layoutget_res.flist[i].fh_len;
		memcpy(&flp->lg_fh_list[i].fh_base,
		       res.layoutget_res.flist[i].fh_val,
		       res.layoutget_res.flist[i].fh_len);
	}

	/* encode the layoutget body */
	status = lgp->func(&lgp->xdr, flp);

layoutget_cleanup:
	if (flp) {
		if (flp->lg_fh_list)
			kfree(flp->lg_fh_list);
		kfree(flp);
	}

	return status;
}

int
spnfs_layoutcommit(void)
{
	return 0;
}

int
spnfs_layoutreturn(struct inode *inode, void *pnfs_layout_return_p)
{
	return 0;
}

int
spnfs_getdeviceiter(struct super_block *sb, struct pnfs_deviter_arg *iter)
{
	struct spnfs *spnfs = global_spnfs;   /* XXX keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	int status = 0;

	im.im_type = SPNFS_TYPE_GETDEVICEITER;
	im.im_args.getdeviceiter_args.cookie = iter->cookie;

	/* call function to queue the msg for upcall */
	status = spnfs_upcall(spnfs, &im, &res);
	if (status != 0) {
		dprintk("%s spnfs upcall failure: %d\n", __func__, status);
		return -EIO;
	}
	status = res.getdeviceiter_res.status;

	if (res.getdeviceiter_res.eof)
		iter->eof = 1;
	else {
		iter->devid = res.getdeviceiter_res.devid;
		iter->cookie = res.getdeviceiter_res.cookie;
		iter->verf = res.getdeviceiter_res.verf;
		iter->eof = 0;
	}

	return status;
}

int
spnfs_getdeviceinfo(struct super_block *sb, struct pnfs_devinfo_arg *info)
{
	struct spnfs *spnfs = global_spnfs;
	struct spnfs_msg im;
	union spnfs_msg_res res;
	struct spnfs_device *dev;
	struct pnfs_filelayout_device *fldev = NULL;
	struct pnfs_filelayout_multipath *mp = NULL;
	struct pnfs_filelayout_devaddr *fldap = NULL;
	int status = 0, i, len;

	im.im_type = SPNFS_TYPE_GETDEVICEINFO;
	/* XXX FIX: figure out what to do about fsid */
	im.im_args.getdeviceinfo_args.devid = info->devid.pnfs_devid;

	/* call function to queue the msg for upcall */
	status = spnfs_upcall(spnfs, &im, &res);
	if (status != 0) {
		dprintk("%s spnfs upcall failure: %d\n", __func__, status);
		status = -EIO;
		goto getdeviceinfo_out;
	}
	status = res.getdeviceinfo_res.status;
	if (status != 0)
		goto getdeviceinfo_out;

	dev = &res.getdeviceinfo_res.devinfo;

	/* Fill in the device data, i.e., nfs4_1_file_layout_ds_addr4 */
	fldev = kmalloc(sizeof(struct pnfs_filelayout_device), GFP_KERNEL);
	if (fldev == NULL) {
		status = -ENOMEM;
		goto getdeviceinfo_out;
	}
	fldev->fl_stripeindices_list = NULL;
	fldev->fl_device_list = NULL;

	/*
	 * Stripe count is the same as data server count for our purposes
	 */
	fldev->fl_stripeindices_length = dev->dscount;
	fldev->fl_device_length = dev->dscount;

	/* Set stripe indices */
	fldev->fl_stripeindices_list =
		kmalloc(fldev->fl_stripeindices_length * sizeof(u32),
			GFP_KERNEL);
	if (fldev->fl_stripeindices_list == NULL) {
		status = -ENOMEM;
		goto getdeviceinfo_out;
	}
	for (i = 0; i < fldev->fl_stripeindices_length; i++)
		fldev->fl_stripeindices_list[i] = i;

	/*
	 * Set the device's data server addresses  No multipath for spnfs,
	 * so mp length is always 1.
	 *
	 */
	fldev->fl_device_list =
		kmalloc(fldev->fl_device_length *
			sizeof(struct pnfs_filelayout_multipath),
			GFP_KERNEL);
	if (fldev->fl_device_list == NULL) {
		status = -ENOMEM;
		goto getdeviceinfo_out;
	}
	for (i = 0; i < fldev->fl_device_length; i++) {
		mp = &fldev->fl_device_list[i];
		mp->fl_multipath_length = 1;
		mp->fl_multipath_list =
			kmalloc(sizeof(struct pnfs_filelayout_devaddr),
				GFP_KERNEL);
		if (mp->fl_multipath_list == NULL) {
			status = -ENOMEM;
			goto getdeviceinfo_out;
		}
		fldap = mp->fl_multipath_list;

		/*
		 * Copy the netid into the device address, for example: "tcp"
		 */
		len = strlen(dev->dslist[i].netid);
		fldap->r_netid.data = kmalloc(len, GFP_KERNEL);
		if (fldap->r_netid.data == NULL) {
			status = -ENOMEM;
			goto getdeviceinfo_out;
		}
		memcpy(fldap->r_netid.data, dev->dslist[i].netid, len);
		fldap->r_netid.len = len;

		/*
		 * Copy the network address into the device address,
		 * for example: "10.35.9.16.08.01"
		 */
		len = strlen(dev->dslist[i].addr);
		fldap->r_addr.data = kmalloc(len, GFP_KERNEL);
		if (fldap->r_addr.data == NULL) {
			status = -ENOMEM;
			goto getdeviceinfo_out;
		}
		memcpy(fldap->r_addr.data, dev->dslist[i].addr, len);
		fldap->r_addr.len = len;
	}

	/* XXX FIX: this should go through the userspace daemon */
	info->notify_types = 0;

	/* encode the device data */
	status = info->func(&info->xdr, fldev);

getdeviceinfo_out:
	if (fldev) {
		kfree(fldev->fl_stripeindices_list);
		if (fldev->fl_device_list) {
			for (i = 0; i < fldev->fl_device_length; i++) {
				fldap =
				    fldev->fl_device_list[i].fl_multipath_list;
				kfree(fldap->r_netid.data);
				kfree(fldap->r_addr.data);
				kfree(fldap);
			}
			kfree(fldev->fl_device_list);
		}
		kfree(fldev);
	}

	return status;
}

int
spnfs_setattr(void)
{
	return 0;
}

int
spnfs_open(struct inode *inode, void *p)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	struct nfsd4_pnfs_open *poa;
	int status = 0;

	poa = (struct nfsd4_pnfs_open *)p;
	im.im_type = SPNFS_TYPE_OPEN;
	im.im_args.open_args.inode = inode->i_ino;
	im.im_args.open_args.create = poa->op_create;
	im.im_args.open_args.createmode = poa->op_createmode;
	im.im_args.open_args.truncate = poa->op_truncate;

	/* call function to queue the msg for upcall */
	status = spnfs_upcall(spnfs, &im, &res);
	if (status != 0) {
		dprintk("%s spnfs upcall failure: %d\n", __func__, status);
		status = -EIO;
		goto open_out;
	}
	status = res.open_res.status;

	/*
	 * Some temporary hackery to test layoutrecall.  If the file
	 * starts with .recall, save its inode.  Upon close of this file,
	 * a layoutrecall will be triggered.
	 */
	if (strncmp(poa->op_fn, ".recall", 7) == 0) {
		recall_inode = inode;
		dprintk("%s: will recall layout for %s, ino = %lu\n",
			__func__, poa->op_fn, inode->i_ino);
	}

open_out:
	return status;
}

/* MSXXX: some temporary hackery used to test layoutrecall */
int
spnfs_close(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct nfsd4_pnfs_cb_layout lr;

	/* trigger layoutrecall */
	if (inode == recall_inode) {
		dprintk("%s: recalling layout for ino = %lu\n",
			__func__, inode->i_ino);
		recall_inode = NULL;

		lr.cbl_recall_type = RECALL_FILE;
		lr.cbl_seg.layout_type = LAYOUT_NFSV4_FILES;
		lr.cbl_seg.clientid = 0;
		lr.cbl_seg.offset = 0;
		lr.cbl_seg.length = NFS4_LENGTH_EOF;
		lr.cbl_seg.iomode = IOMODE_ANY;
		lr.cbl_layoutchanged = 0;

		sb->s_export_op->cb_layout_recall(sb, inode, &lr);
	}

	return 0;
}

int
spnfs_create(void)
{
	return 0;
}

/*
 * Invokes the spnfsd with the inode number of the object to remove.
 * The file has already been removed on the MDS, so all the spnsfd
 * daemon does is remove the stripes.
 * Returns 0 on success otherwise error code
 */
int
spnfs_remove(unsigned long ino)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	int status = 0;

	im.im_type = SPNFS_TYPE_REMOVE;
	im.im_args.remove_args.inode = ino;

	/* call function to queue the msg for upcall */
	status = spnfs_upcall(spnfs, &im, &res);
	if (status != 0) {
		dprintk("%s spnfs upcall failure: %d\n", __func__, status);
		status = -EIO;
		goto remove_out;
	}
	status = res.remove_res.status;

remove_out:
	return status;
}

int
spnfs_read_one(unsigned long ino, loff_t offset, size_t len, char *buf)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	int status = 0;
	unsigned long todo = len;
	unsigned long bytecount = 0;

	im.im_type = SPNFS_TYPE_READ;
	im.im_args.read_args.inode = ino;
	while (todo > 0) {
		im.im_args.read_args.offset = offset;
		if (todo > SPNFS_MAX_IO)
			im.im_args.read_args.len = SPNFS_MAX_IO;
		else
			im.im_args.read_args.len = todo;
		/* call function to queue the msg for upcall */
		status = spnfs_upcall(spnfs, &im, &res);
		if (status != 0) {
			dprintk("%s spnfs upcall failure: %d\n",
				__func__, status);
			status = -EIO;
			goto read_out;
		}
		/* status < 0 => error, status > 0 => bytes moved */
		status = res.read_res.status;
		if (status < 0) {
			dprintk("%s spnfs read failure: %d\n",
				__func__, status);
			status = -EIO;
			goto read_out;
		}
		/* status == 0, maybe eof.  not making forward progress */
		if (status == 0) {
			status = bytecount;
			goto read_out;
		}
		/* status = number of bytes successfully i/o'd */
		memcpy(buf, res.read_res.data, status);
		buf += status;
		offset += status;
		bytecount += status;
		todo -= status;
	}
	status = bytecount;

read_out:
	return status;
}

int
spnfs_read(unsigned long ino, loff_t offset, unsigned long *lenp, int vlen,
		struct svc_rqst *rqstp)
{
	int vnum, err, bytecount = 0;
	size_t iolen;

	for (vnum = 0 ; vnum < vlen ; vnum++) {
		iolen = rqstp->rq_vec[vnum].iov_len;
		err = spnfs_read_one(ino, offset + bytecount, iolen,
				(char *)rqstp->rq_vec[vnum].iov_base);
		if (err < 0)
			return -EIO;
		if (err < iolen) {
			bytecount += err;
			goto out;
		}
		bytecount += rqstp->rq_vec[vnum].iov_len;
	}

out:
	*lenp = bytecount;
	return 0;
}

int
spnfs_write_one(unsigned long ino, loff_t offset, size_t len, char *buf)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	int status = 0;
	size_t todo = len;
	unsigned long bytecount = 0;

	im.im_type = SPNFS_TYPE_WRITE;
	im.im_args.write_args.inode = ino;
	while (todo > 0) {
		im.im_args.write_args.offset = offset;
		if (todo > SPNFS_MAX_IO)
			im.im_args.write_args.len = SPNFS_MAX_IO;
		else
			im.im_args.write_args.len = todo;
		memcpy(im.im_args.write_args.data, buf,
			im.im_args.write_args.len);
		/* call function to queue the msg for upcall */
		status = spnfs_upcall(spnfs, &im, &res);
		if (status != 0) {
			dprintk("%s spnfs upcall failure: %d\n",
				__func__, status);
			status = -EIO;
			goto write_out;
		}
		/* status < 0 => error, status > 0 => bytes moved */
		status = res.write_res.status;
		if (status < 0) {
			dprintk("%s spnfs write failure: %d\n",
				__func__, status);
			status = -EIO;
			goto write_out;
		}
		/* status == 0.  not making forward progress */
		if (status == 0) {
			dprintk("spnfs write no forward progress\n");
			status = bytecount;
			goto write_out;
		}
		/* status = number of bytes successfully i/o'd */
		buf += status;
		offset += status;
		bytecount += status;
		todo -= status;
	}
	status = bytecount;

write_out:
	return status;
}

int
spnfs_write(unsigned long ino, loff_t offset, size_t len, int vlen,
		struct svc_rqst *rqstp)
{
	int vnum, err, bytecount = 0;
	size_t iolen;

	for (vnum = 0 ; vnum < vlen ; vnum++) {
		iolen = rqstp->rq_vec[vnum].iov_len;
		err = spnfs_write_one(ino, offset + bytecount, iolen,
				(char *)rqstp->rq_vec[vnum].iov_base);
		if (err != iolen) {
			dprintk("err=%d expected %Zd\n", err, len);
			return -EIO;
		}
		bytecount += rqstp->rq_vec[vnum].iov_len;
	}

	return 0;
}

int
spnfs_commit(void)
{
	return 0;
}

/*
 * Return the state for this object.
 * At this time simply return 0 to indicate success and use the existing state
 */
int
spnfs_get_state(struct inode *inode, void *fh, void *state)
{
	return 0;
}

/*
 * Return the filehandle for the specified file descriptor
 */
int
spnfs_getfh(int fd, struct nfs_fh *fh)
{
	struct file *file;

	file = fget(fd);
	if (file == NULL)
		return -EIO;

	memcpy(fh, NFS_FH(file->f_dentry->d_inode), sizeof(struct nfs_fh));
	fput(file);
	return 0;
}

#endif /* CONFIG_SPNFS */
