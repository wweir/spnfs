/*
 * include/linux/nfsd4_spnfs.h
 *
 * spNFS - simple pNFS implementation with userspace daemon
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

#ifndef NFS_SPNFS_H
#define NFS_SPNFS_H


#ifdef __KERNEL__
#include "sunrpc/svc.h"
#include "nfsd/nfsfh.h"
#else
#include <sys/types.h>
#endif /* __KERNEL__ */

#define SPNFS_STATUS_INVALIDMSG		0x01
#define SPNFS_STATUS_AGAIN		0x02
#define SPNFS_STATUS_FAIL		0x04
#define SPNFS_STATUS_SUCCESS		0x08

#define SPNFS_TYPE_GETDEVICELIST	0x04
#define SPNFS_TYPE_GETDEVICEINFO	0x05
#define	SPNFS_TYPE_CLOSE		0x08

/* getdevicelist */
struct spnfs_msg_getdevicelist_args {
	unsigned long inode;
};

struct spnfs_getdevicelist_dev {
	u_int32_t devid;
	char netid[5];
	char addr[29];
};

struct spnfs_msg_getdevicelist_res {
	int status;
	int count;
	struct spnfs_getdevicelist_dev dlist[SPNFS_MAX_DATA_SERVERS];
};

/* getdeviceinfo */
struct spnfs_msg_getdeviceinfo_args {
	u_int32_t devid;
};

struct spnfs_msg_getdeviceinfo_res {
	int status;
	struct spnfs_getdevicelist_dev dinfo;
};

/* close */
/* No op for daemon */
struct spnfs_msg_close_args {
	int x;
};

struct spnfs_msg_close_res {
	int y;
};

/* bundle args and responses */
union spnfs_msg_args {
	struct spnfs_msg_getdevicelist_args     getdevicelist_args;
	struct spnfs_msg_getdeviceinfo_args     getdeviceinfo_args;
	struct spnfs_msg_close_args		close_args;
};

union spnfs_msg_res {
	struct spnfs_msg_getdevicelist_res      getdevicelist_res;
	struct spnfs_msg_getdeviceinfo_res      getdeviceinfo_res;
	struct spnfs_msg_close_res		close_res;
};

/* a spnfs message, args and response */
struct spnfs_msg {
	unsigned char		im_type;
	unsigned char		im_status;
	union spnfs_msg_args	im_args;
	union spnfs_msg_res	im_res;
};

#ifdef __KERNEL__

/* pipe mgmt structure.  messages flow through here */
struct spnfs {
	char			spnfs_path[48];   /* path to pipe */
	struct dentry		*spnfs_dentry;    /* dentry for pipe */
	wait_queue_head_t	spnfs_wq;
	struct spnfs_msg	spnfs_im;         /* spnfs message */
	struct mutex		spnfs_lock;       /* Serializes upcalls */
	struct mutex		spnfs_plock;
};

int spnfs_getdevicelist(struct super_block *, void *);
int spnfs_getdeviceinfo(struct super_block *, void *);

int nfsd_spnfs_new(void);
void nfsd_spnfs_delete(void);
int spnfs_upcall(struct spnfs *, struct spnfs_msg *, union spnfs_msg_res *);
int spnfs_enabled(void);

#endif /* __KERNEL__ */

#endif /* NFS_SPNFS_H */
