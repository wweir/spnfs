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
#include <linux/nfsd4_spnfs.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/nfsd4_pnfs.h>
#include <linux/nfsd/nfs4layoutxdr.h>

#define	NFSDDBG_FACILITY		NFSDDBG_PROC

/*
 * The functions that are called from elsewhere in the kernel
 * to perform tasks in userspace
 *
 */

#if defined(CONFIG_PNFSD)

extern struct spnfs *global_spnfs;

int
spnfs_layout_type(void)
{
	return LAYOUT_NFSV4_FILES;
}

int
spnfs_layoutget(struct inode *inode, void *pnfs_layout_get_p)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;
	struct nfsd4_pnfs_layoutget *lgp =
		(struct nfsd4_pnfs_layoutget *)pnfs_layout_get_p;
	struct nfsd4_pnfs_filelayout *flp = NULL;
	struct nfsd4_pnfs_layoutlist *lp = NULL;
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

	lgp->lg_return_on_close = 0;
	flp = kmalloc(sizeof(*flp), GFP_KERNEL);
	if (!flp)
		goto layoutget_cleanup;
	lgp->lg_layout = flp;
	flp->lg_stripe_type = res.layoutget_res.stripe_type;
	flp->lg_commit_through_mds = 0;
	flp->lg_stripe_unit =  res.layoutget_res.stripe_size;
	/*
	 * XXX Should I get the size through a geattr instead?
	 *     or is this size guaranteed to be valid?
	 */
	flp->lg_file_size =  inode->i_size;
	flp->lg_indexlen = 0;	/* XXX Does not support stripe indices */
	flp->lg_llistlen = res.layoutget_res.layout_count;
	lgp->lg_seg.length = NFS4_LENGTH_EOF;
	lp = kmalloc(flp->lg_llistlen * sizeof(*lp), GFP_KERNEL);
	if (!lp) {
		status = -ENOMEM;
		goto layoutget_cleanup;
	}
	flp->lg_llist = lp;
	for (i = 0; i < res.layoutget_res.layout_count; i++) {
		/*
		 *
		 */
#if 0
		lp->dev_layout_type = lgp->lg_type; /* XXX Should ask daemon */
#endif
		lp->dev_id = res.layoutget_res.flist[i].dev_id;
		lp->dev_index = res.layoutget_res.flist[i].dev_index;
/*
		lp->fhp = kmalloc(sizeof(*lp->fhp), GFP_KERNEL);
		if (lp->fhp == NULL) {
			status = -ENOMEM;
			goto layoutget_cleanup;
		}
		lp->fhp->fh_size = res.layoutget_res.flist[i].fh_len;
		memcpy(lp->fhp->fh_base.fh_pad,
		       (void *)res.layoutget_res.flist[i].fh_val, 128);
*/
		lp->dev_fh.fh_size = res.layoutget_res.flist[i].fh_len;
		memcpy(lp->dev_fh.fh_base.fh_pad,
		       (void *)res.layoutget_res.flist[i].fh_val, 128);
		lp++;
	}

	return status;

layoutget_cleanup:
	if (flp) {
		if (flp->lg_llist) {
/*
			lp = flp->lg_llist;
			for (i = 0; i < flp->lg_llistlen; i++) {
				if (lp->fhp)
					kfree(lp->fhp);
				lp++;
			}
*/
			kfree(flp->lg_llist);
		}
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
spnfs_getdevicelist(struct super_block *sb, void *get_dev_list_arg_p)
{
	struct spnfs *spnfs = global_spnfs;   /* XXX keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;

	struct nfsd4_pnfs_getdevlist *gdlp = NULL;
	struct nfsd4_pnfs_devlist *dlp = NULL, *item = NULL;
	struct pnfs_filelayout_devaddr *fldap = NULL;
	int status = 0;
	int i = 0, count = 0, len = 0;
	char *strp = NULL;

	im.im_type = SPNFS_TYPE_GETDEVICELIST;
	im.im_args.getdevicelist_args.inode = sb->s_root->d_inode->i_ino;

	/* call function to queue the msg for upcall */
	if (spnfs_upcall(spnfs, &im, &res) != 0) {
		dprintk("failed spnfs upcall: getdevicelist\n");
		status = -EIO;
		goto gdevl_cleanup;
	}
	count = res.getdevicelist_res.count;

	dlp = kmalloc(count * sizeof(*dlp), GFP_KERNEL);
	if (!dlp) {
		status = -ENOMEM;
		goto gdevl_cleanup;
	}

	gdlp = (struct nfsd4_pnfs_getdevlist *)get_dev_list_arg_p;
	gdlp->gd_type = 1L;
	gdlp->gd_cookie = 0LL;
	gdlp->gd_verf = 0LL;
	gdlp->gd_ops = sb->s_export_op;
	gdlp->gd_devlist_len = res.getdevicelist_res.count;
	gdlp->gd_devlist = dlp;
	gdlp->gd_eof = 1;

	item = dlp;
	for (i = 0; i < count; i++) {
		/*
		 * Copy the device ID
		 */
		item->dev_id = res.getdevicelist_res.dlist[i].devid;

		/*
		 * Build the device address
		 */
		fldap = kmalloc(sizeof(*fldap), GFP_KERNEL);
		if (!fldap) {
			status = -ENOMEM;
			goto gdevl_cleanup;
		}

		/*
		 * Copy the netid into the device address, for example: "tcp"
		 */
		strp = res.getdevicelist_res.dlist[i].netid;
		len = strlen(strp);
		fldap->r_netid.data = kmalloc(len, GFP_KERNEL);
		if (fldap->r_netid.data == NULL)
			goto gdevl_cleanup;
		memcpy(fldap->r_netid.data,
			res.getdevicelist_res.dlist[i].netid, len);
		fldap->r_netid.len = len;

		/*
		 * Copy the network address into the device address,
		 * for example: "10.35.9.16.08.01"
		 */
		strp = res.getdevicelist_res.dlist[i].addr;
		len = strlen(strp);
		fldap->r_addr.data = kmalloc(len, GFP_KERNEL);
		if (fldap->r_addr.data == NULL)
			goto gdevl_cleanup;
		memcpy(fldap->r_addr.data,
		       res.getdevicelist_res.dlist[i].addr, len);
		fldap->r_addr.len = len;

		item->dev_addr = fldap;

		/*
		 * Work on the next devlist item
		 */
		item++;
	}

	return status;

gdevl_cleanup:
	if (dlp) {
		item = dlp;
		for (i = 0; i < count; i++) {
			fldap = item->dev_addr;
			if (fldap) {
				kfree(fldap->r_netid.data);
				kfree(fldap->r_addr.data);
			}
			kfree(fldap);
			item++;
		}
		kfree(dlp);
	}

	return status;
}

int
spnfs_getdeviceinfo(struct super_block *sb, void *p)
{
	struct spnfs *spnfs = global_spnfs;
	struct nfsd4_pnfs_getdevinfo *gdinfo;
	struct spnfs_msg im;
	union spnfs_msg_res res;
	struct pnfs_filelayout_devaddr *fldap = NULL;
	int len, status;
	char *strp;

	gdinfo = (struct nfsd4_pnfs_getdevinfo *)p;
	im.im_type = SPNFS_TYPE_GETDEVICEINFO;
	im.im_args.getdeviceinfo_args.devid = gdinfo->gd_dev_id;
	/* call function to queue the msg for upcall */
	status = spnfs_upcall(spnfs, &im, &res);
	if (status != 0) {
		dprintk("%s spnfs upcall failure: %d\n", __FUNCTION__, status);
		status = -EIO;
		goto getdeviceinfo_out;
	}
	status = res.open_res.status;

	gdinfo->gd_type = 1L;
	gdinfo->gd_devlist_len = 1L; /* DMXXX why is there a len here? */

	fldap = kmalloc(sizeof(*fldap), GFP_KERNEL);
	if (!fldap) {
		status = -ENOMEM;
		fldap = NULL;
		goto getdeviceinfo_out;
	}
	strp = res.getdeviceinfo_res.dinfo.netid;
	len = strlen(strp);
	fldap->r_netid.data = kmalloc(len, GFP_KERNEL);
	if (fldap->r_netid.data == NULL) {
		kfree(fldap);
		status = -ENOMEM;
		fldap = NULL;
		goto getdeviceinfo_out;
	}
	memcpy(fldap->r_netid.data, res.getdeviceinfo_res.dinfo.netid, len);
	fldap->r_netid.len = len;

	/*
	 * Copy the network address into the device address,
	 * for example: "10.35.9.16.08.01"
	 */
	strp = res.getdeviceinfo_res.dinfo.addr;
	len = strlen(strp);
	fldap->r_addr.data = kmalloc(len, GFP_KERNEL);
	if (fldap->r_addr.data == NULL) {
		kfree(fldap->r_netid.data);
		kfree(fldap);
		fldap = NULL;
		status = -ENOMEM;
		goto getdeviceinfo_out;
	}
	memcpy(fldap->r_addr.data, res.getdeviceinfo_res.dinfo.addr, len);
	fldap->r_addr.len = len;

getdeviceinfo_out:
	gdinfo->gd_devaddr = (void *)fldap;
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
		dprintk("%s spnfs upcall failure: %d\n", __FUNCTION__, status);
		status = -EIO;
		goto open_out;
	}
	status = res.open_res.status;

open_out:
	return status;
}

/* DMXXX: this is only a test function atm.  Unrelated to close. */
int
spnfs_close(void)
{
	struct spnfs *spnfs = global_spnfs; /* keep up the pretence */
	struct spnfs_msg im;
	union spnfs_msg_res res;

	im.im_type = SPNFS_TYPE_CLOSE;
	im.im_args.close_args.x = 1337;

	/* call generic function to queue the msg for upcall */
	if (spnfs_upcall(spnfs, &im, &res) == 0) {
		dprintk("spnfs_close success: %d\n", res.close_res.y);
	} else {
		dprintk("failed spnfs upcall: close\n");
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
		dprintk("%s spnfs upcall failure: %d\n", __FUNCTION__, status);
		status = -EIO;
		goto remove_out;
	}
	status = res.remove_res.status;

remove_out:
	return status;
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
struct nfs_fh *
spnfs_getfh(int fd)
{
	struct file *file;

	file = fget(fd);
	if (file == NULL)
		return NULL;

	return(NFS_FH(file->f_dentry->d_inode));
}

#endif /* CONFIG_PNFSD */
