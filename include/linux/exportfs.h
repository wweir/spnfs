#ifndef LINUX_EXPORTFS_H
#define LINUX_EXPORTFS_H 1

#include <linux/types.h>

struct dentry;
struct inode;
struct super_block;
struct vfsmount;

/*
 * The fileid_type identifies how the file within the filesystem is encoded.
 * In theory this is freely set and parsed by the filesystem, but we try to
 * stick to conventions so we can share some generic code and don't confuse
 * sniffers like ethereal/wireshark.
 *
 * The filesystem must not use the value '0' or '0xff'.
 */
enum fid_type {
	/*
	 * The root, or export point, of the filesystem.
	 * (Never actually passed down to the filesystem.
	 */
	FILEID_ROOT = 0,

	/*
	 * 32bit inode number, 32 bit generation number.
	 */
	FILEID_INO32_GEN = 1,

	/*
	 * 32bit inode number, 32 bit generation number,
	 * 32 bit parent directory inode number.
	 */
	FILEID_INO32_GEN_PARENT = 2,
};

#if defined(CONFIG_PNFSD)

/* XDR stream arguments and results.  Exported file system uses this
 * struct to encode information and return how many bytes were encoded.
 */
struct pnfs_xdr_info {
	u32 *p;			/* in */
	u32 *end;		/* in */
	u32 maxcount;		/* in */
	u32 bytes_written;	/* out */
};

/* Used by get_device_info to encode a device (da_addr_body in spec)
 * Args:
 * xdr - xdr stream
 * device - pointer to device to be encoded
*/
typedef int (*pnfs_encodedev_t)(struct pnfs_xdr_info *xdr, void *device);

/* Arguments for get_device_info */
struct pnfs_devinfo_arg {
	u32 type;
	u32 devid;
	struct pnfs_xdr_info xdr;
	pnfs_encodedev_t func;
};

/* Used by get_device_iter to retrieve all available devices.
 * Args:
 * gld_type - layout type
 * gld_cookie/verf - index and verifier of current list item
 * gld_devid - output device id
 */
struct pnfs_deviter_arg {
	u32 type;	/* request */
	u64 cookie;	/* request/response */
	u64 verf;	/* request/response */
	u32 devid;	/* response */
	u32 eof;	/* response */
};

struct nfsd4_layout_seg {
	u64	clientid;
	u32	layout_type;
	u32	iomode;
	u64	offset;
	u64	length;
};

/* Used by layout_get to encode layout (loc_body var in spec)
 * Args:
 * xdr - xdr stream
 * layout - pointer to layout to be encoded
 * TODO: use common func with dev?
 */
typedef int (*pnfs_encodelayout_t)(struct pnfs_xdr_info *xdr, void *layout);

/* Arguments for layoutget */
struct pnfs_layoutget_arg {
	u64			minlength;	/* request */
	pnfs_encodelayout_t 	func;		/* request */
	struct knfsd_fh		*fh;		/* request/response */
	struct nfsd4_layout_seg	seg;		/* request/response */
	struct pnfs_xdr_info	xdr;		/* request/response */
	u32			return_on_close;/* response */
};

#endif /* CONFIG_PNFSD */

struct fid {
	union {
		struct {
			u32 ino;
			u32 gen;
			u32 parent_ino;
			u32 parent_gen;
		} i32;
		__u32 raw[6];
	};
};

/**
 * struct export_operations - for nfsd to communicate with file systems
 * @encode_fh:      encode a file handle fragment from a dentry
 * @fh_to_dentry:   find the implied object and get a dentry for it
 * @fh_to_parent:   find the implied object's parent and get a dentry for it
 * @get_name:       find the name for a given inode in a given directory
 * @get_parent:     find the parent of a given directory
 *
 * See Documentation/filesystems/Exporting for details on how to use
 * this interface correctly.
 *
 * encode_fh:
 *    @encode_fh should store in the file handle fragment @fh (using at most
 *    @max_len bytes) information that can be used by @decode_fh to recover the
 *    file refered to by the &struct dentry @de.  If the @connectable flag is
 *    set, the encode_fh() should store sufficient information so that a good
 *    attempt can be made to find not only the file but also it's place in the
 *    filesystem.   This typically means storing a reference to de->d_parent in
 *    the filehandle fragment.  encode_fh() should return the number of bytes
 *    stored or a negative error code such as %-ENOSPC
 *
 * fh_to_dentry:
 *    @fh_to_dentry is given a &struct super_block (@sb) and a file handle
 *    fragment (@fh, @fh_len). It should return a &struct dentry which refers
 *    to the same file that the file handle fragment refers to.  If it cannot,
 *    it should return a %NULL pointer if the file was found but no acceptable
 *    &dentries were available, or an %ERR_PTR error code indicating why it
 *    couldn't be found (e.g. %ENOENT or %ENOMEM).  Any suitable dentry can be
 *    returned including, if necessary, a new dentry created with d_alloc_root.
 *    The caller can then find any other extant dentries by following the
 *    d_alias links.
 *
 * fh_to_parent:
 *    Same as @fh_to_dentry, except that it returns a pointer to the parent
 *    dentry if it was encoded into the filehandle fragment by @encode_fh.
 *
 * get_name:
 *    @get_name should find a name for the given @child in the given @parent
 *    directory.  The name should be stored in the @name (with the
 *    understanding that it is already pointing to a a %NAME_MAX+1 sized
 *    buffer.   get_name() should return %0 on success, a negative error code
 *    or error.  @get_name will be called without @parent->i_mutex held.
 *
 * get_parent:
 *    @get_parent should find the parent directory for the given @child which
 *    is also a directory.  In the event that it cannot be found, or storage
 *    space cannot be allocated, a %ERR_PTR should be returned.
 *
 * Locking rules:
 *    get_parent is called with child->d_inode->i_mutex down
 *    get_name is not (which is possibly inconsistent)
 */

struct export_operations {
	int (*encode_fh)(struct dentry *de, __u32 *fh, int *max_len,
			int connectable);
	struct dentry * (*fh_to_dentry)(struct super_block *sb, struct fid *fid,
			int fh_len, int fh_type);
	struct dentry * (*fh_to_parent)(struct super_block *sb, struct fid *fid,
			int fh_len, int fh_type);
	int (*get_name)(struct dentry *parent, char *name,
			struct dentry *child);
	struct dentry * (*get_parent)(struct dentry *child);
#if defined(CONFIG_PNFSD)
	/* pNFS operations */
		/* pNFS: returns the verifier */
	void (*get_verifier) (struct super_block *sb, u32 *p);
		/* pNFS: Returns the supported pnfs_layouttype4. */
	int (*layout_type)(void);
	/* Retrieve and encode a device onto the xdr stream.
	 * Args:
	 * sb - superblock
	 * arg - layout type, device id, maxcount
	 * arg.xdr - xdr stream for encoding
	 * arg.func - Optional function called by file system to encode
	 * device on xdr stream.
	 */
	int (*get_device_info) (struct super_block *sb,
				struct pnfs_devinfo_arg *arg);
	/* Retrieve all available devices via an iterator */
	int (*get_device_iter) (struct super_block *sb,
				struct pnfs_deviter_arg *arg);
		/* can layout segments be merged for this layout type? */
	int (*can_merge_layouts)(u32 layout_type);
	/* Retrieve and encode a layout onto the xdr stream.
	 * Args:
	 * inode - inode for which to retrieve layout
	 * arg.xdr - xdr stream for encoding
	 * arg.func - Optional function called by file system to encode
	 * layout on xdr stream.
	 */
	int (*layout_get) (struct inode *inode,
			   struct pnfs_layoutget_arg *arg);
		/* pNFS: commit changes to layout */
	int (*layout_commit) (struct inode *inode, void *p);
		/* pNFS: returns the layout */
	int (*layout_return) (struct inode *inode, void *p);


		/* callback from fs on MDS only */
	int (*cb_get_state) (struct super_block *sb, void *state);
	int (*cb_layout_recall) (struct super_block *sb, struct inode *inode, void *p);
		/* call fs on DS only */
	int (*get_state) (struct inode *inode, void *fh, void *state);
		/* callback from fs on DS only */
	int (*cb_change_state) (void *p);
#endif /* CONFIG_PNFSD */
};

extern int exportfs_encode_fh(struct dentry *dentry, struct fid *fid,
	int *max_len, int connectable);
extern struct dentry *exportfs_decode_fh(struct vfsmount *mnt, struct fid *fid,
	int fh_len, int fileid_type, int (*acceptable)(void *, struct dentry *),
	void *context);

/*
 * Generic helpers for filesystems.
 */
extern struct dentry *generic_fh_to_dentry(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, u64 ino, u32 gen));
extern struct dentry *generic_fh_to_parent(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, u64 ino, u32 gen));

#endif /* LINUX_EXPORTFS_H */
