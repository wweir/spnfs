/*
 *	fs/nfsctl.c
 *
 *	This should eventually move to userland.
 *
 */
#include <linux/types.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/syscall.h>
#include <linux/linkage.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <linux/module.h>

/*
 * open a file on nfsd fs
 */

static struct file *do_open(char *name, int flags)
{
	struct nameidata nd;
	struct vfsmount *mnt;
	int error;

	mnt = do_kern_mount("nfsd", 0, "nfsd", NULL);
	if (IS_ERR(mnt))
		return (struct file *)mnt;

	error = vfs_path_lookup(mnt->mnt_root, mnt, name, 0, &nd);
	mntput(mnt);	/* drop do_kern_mount reference */
	if (error)
		return ERR_PTR(error);

	if (flags == O_RDWR)
		error = may_open(&nd,MAY_READ|MAY_WRITE,FMODE_READ|FMODE_WRITE);
	else
		error = may_open(&nd, MAY_WRITE, FMODE_WRITE);

	if (!error)
		return dentry_open(nd.path.dentry, nd.path.mnt, flags);

	path_put(&nd.path);
	return ERR_PTR(error);
}

static struct {
	char *name; int wsize; int rsize;
} map[] = {
	[NFSCTL_SVC] = {
		.name	= ".svc",
		.wsize	= sizeof(struct nfsctl_svc)
	},
	[NFSCTL_ADDCLIENT] = {
		.name	= ".add",
		.wsize	= sizeof(struct nfsctl_client)
	},
	[NFSCTL_DELCLIENT] = {
		.name	= ".del",
		.wsize	= sizeof(struct nfsctl_client)
	},
	[NFSCTL_EXPORT] = {
		.name	= ".export",
		.wsize	= sizeof(struct nfsctl_export)
	},
	[NFSCTL_UNEXPORT] = {
		.name	= ".unexport",
		.wsize	= sizeof(struct nfsctl_export)
	},
	[NFSCTL_GETFD] = {
		.name	= ".getfd",
		.wsize	= sizeof(struct nfsctl_fdparm),
		.rsize	= NFS_FHSIZE
	},
	[NFSCTL_GETFS] = {
		.name	= ".getfs",
		.wsize	= sizeof(struct nfsctl_fsparm),
		.rsize	= sizeof(struct knfsd_fh)
	},
};

int (*spnfs_init)(void);
int (*spnfs_test)(void);
void (*spnfs_delete)(void);
struct nfs_fh * (*spnfs_getfh_vec)(int);
EXPORT_SYMBOL(spnfs_init);
EXPORT_SYMBOL(spnfs_test);
EXPORT_SYMBOL(spnfs_delete);
EXPORT_SYMBOL(spnfs_getfh_vec);

long
asmlinkage sys_nfsservctl(int cmd, struct nfsctl_arg __user *arg, void __user *res)
{
	struct file *file;
	void __user *p = &arg->u;
	int version;
	int err;
	int fd;
	struct nfs_fh *fh;
	extern struct nfs_fh *spnfs_getfh(int);

	if (cmd == 222) {
		if (spnfs_init) {
			err = spnfs_init();
			return err;
		} else
			return -EOPNOTSUPP;
	}

	if (cmd == 223) {
		if (spnfs_test) {
			printk(KERN_INFO "nfsctl: spnfs_test\n");
			err = spnfs_test();
			return err;
		} else {
			printk(
			     KERN_INFO "nfsctl: spnfs_test not initialized\n");
			return -EOPNOTSUPP;
		}
	}

	if (cmd == 224) {
		if (spnfs_delete) {
			spnfs_delete();
			return 0;
		} else
			return -EOPNOTSUPP;
	}

	if (cmd == NFSCTL_FD2FH) {
		/*
		 * Shortcut here.  If this cmd lives on, it should probably
		 * be processed like the others below.
		 */
		if (copy_from_user(&fd, &arg->ca_fd2fh.fd, sizeof(int)))
			return -EFAULT;
		if (spnfs_getfh_vec)
			fh = spnfs_getfh_vec(fd);
		else
			return -EINVAL;
		if (fh == NULL)
			return -EINVAL;

		/* XXX fix this with the proper struct */
		if (copy_to_user(res, (char *)fh, 130))
			return -EFAULT;

		return 0;
	}

	if (copy_from_user(&version, &arg->ca_version, sizeof(int)))
		return -EFAULT;

	if (version != NFSCTL_VERSION)
		return -EINVAL;

	if (cmd < 0 || cmd >= ARRAY_SIZE(map) || !map[cmd].name)
		return -EINVAL;

	file = do_open(map[cmd].name, map[cmd].rsize ? O_RDWR : O_WRONLY);	
	if (IS_ERR(file))
		return PTR_ERR(file);
	err = file->f_op->write(file, p, map[cmd].wsize, &file->f_pos);
	if (err >= 0 && map[cmd].rsize)
		err = file->f_op->read(file, res, map[cmd].rsize, &file->f_pos);
	if (err >= 0)
		err = 0;
	fput(file);
	return err;
}
