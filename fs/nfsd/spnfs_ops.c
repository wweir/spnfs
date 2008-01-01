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

#endif /* CONFIG_PNFSD */
