/*
*  linux/fs/nfsd/nfs4pnfsds.c
*
*  Copyright (c) 2005 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Andy Adamson <andros@umich.edu>
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
*
*/

#define NFSDDBG_FACILITY NFSDDBG_PNFS

#include <linux/param.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/debug.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/pnfsd.h>

/*
 *******************
 *   	 PNFS
 *******************
 */
/*
 * Hash tables for pNFS Data Server state
 *
 * mds_nodeid:	list of struct pnfs_mds_id one per Metadata server (MDS) using
 *		this data server (DS).
 *
 * mds_clid_hashtbl[]: uses clientid_hashval(), hash of all clientids obtained
 *			from any MDS.
 *
 * ds_stid_hashtbl[]: uses stateid_hashval(), hash of all stateids obtained
 *			from any MDS.
 *
 */
/* Hash tables for clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
        ((id) & CLIENT_HASH_MASK)

/* hash table for pnfs_ds_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head mds_id_tbl;
static struct list_head mds_clid_hashtbl[CLIENT_HASH_SIZE];
static struct list_head ds_stid_hashtbl[STATEID_HASH_SIZE];

static int
cmp_clid(clientid_t * cl1, clientid_t * cl2) {
	return((cl1->cl_boot == cl2->cl_boot) &&
		(cl1->cl_id == cl2->cl_id));
}

void
nfs4_pnfs_state_init(void)
{
	int i;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&mds_clid_hashtbl[i]);
	}
	for (i = 0; i < STATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&ds_stid_hashtbl[i]);
	}
	INIT_LIST_HEAD(&mds_id_tbl);
}

static struct pnfs_mds_id *
find_pnfs_mds_id(u32 mdsid)
{
	struct pnfs_mds_id *local = NULL;

	dprintk("pNFSD: %s\n",__func__);
	list_for_each_entry(local, &mds_id_tbl, di_hash) {
		if (local->di_mdsid == mdsid)
			return local;
	}
	return NULL;
}

static struct pnfs_ds_clientid *
find_pnfs_ds_clientid(clientid_t *clid)
{
	struct pnfs_ds_clientid *local = NULL;
	unsigned int hashval;

	dprintk("pNFSD: %s\n",__func__);

	hashval = clientid_hashval(clid->cl_id);
	list_for_each_entry(local, &mds_clid_hashtbl[hashval], dc_hash) {
		if (cmp_clid(&local->dc_mdsclid, clid))
			return local;
	}
	return NULL;
}

struct pnfs_ds_stateid *
find_pnfs_ds_stateid(stateid_t *stid)
{
	struct pnfs_ds_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("pNFSD: %s\n",__func__);

	hashval = stateid_hashval(st_id, f_id);
	list_for_each_entry(local, &ds_stid_hashtbl[hashval], ds_hash) {
		if ((local->ds_stid.si_stateownerid == st_id) &&
				(local->ds_stid.si_fileid == f_id) &&
				(local->ds_stid.si_boot == stid->si_boot))
			return local;
	}
	return NULL;
}

static void
release_mds_id(struct pnfs_mds_id *mdp)
{
	dprintk("pNFSD: %s\n",__func__);

	list_del(&mdp->di_hash);
	list_del(&mdp->di_mdsclid);
	kfree (mdp);
}

static void
release_ds_clientid(struct pnfs_ds_clientid *dcp)
{
	dprintk("pNFSD: %s\n",__func__);

	list_del(&dcp->dc_hash);
	list_del(&dcp->dc_stateid);
	list_del(&dcp->dc_permdsid);
	kfree (dcp);
}

static void
release_ds_stateid(struct pnfs_ds_stateid *dsp)
{
	dprintk("pNFSD: %s\n",__func__);

	list_del(&dsp->ds_hash);
	list_del(&dsp->ds_perclid);
	kfree (dsp);
}

static struct pnfs_mds_id *
alloc_init_mds_id(struct pnfs_get_state *gsp)
{
	struct pnfs_mds_id *mdp;

	dprintk("pNFSD: %s\n",__func__);

	mdp = kmalloc(sizeof(*mdp), GFP_KERNEL);
	if (!mdp)
		return NULL;
	INIT_LIST_HEAD(&mdp->di_hash);
	INIT_LIST_HEAD(&mdp->di_mdsclid);
	list_add(&mdp->di_hash, &mds_id_tbl);
	mdp->di_mdsid = gsp->devid;
	mdp->di_mdsboot = 0;
	return mdp;
}

static struct pnfs_ds_clientid *
alloc_init_ds_clientid(struct pnfs_get_state *gsp)
{
	struct pnfs_mds_id *mdp;
	struct pnfs_ds_clientid *dcp;
	unsigned int hashval = clientid_hashval(gsp->clid.cl_id);

	dprintk("pNFSD: %s\n",__func__);

	mdp = find_pnfs_mds_id(gsp->devid);
	if(!mdp)
		mdp = alloc_init_mds_id(gsp);
	if(!mdp)
		return NULL;
	dcp = kmalloc(sizeof(*dcp), GFP_KERNEL);
	if (!dcp) {
		return NULL;
	}
	INIT_LIST_HEAD(&dcp->dc_hash);
	INIT_LIST_HEAD(&dcp->dc_stateid);
	INIT_LIST_HEAD(&dcp->dc_permdsid);
	list_add(&dcp->dc_hash, &mds_clid_hashtbl[hashval]);
	list_add(&dcp->dc_permdsid, &mdp->di_mdsclid);
	dcp->dc_mdsclid = gsp->clid;
	return dcp;
}

static struct pnfs_ds_stateid *
alloc_init_ds_stateid(struct svc_fh *cfh, struct pnfs_get_state *gsp)
{
	struct pnfs_ds_stateid *dsp;
	struct pnfs_ds_clientid *dcp;
	u32 st_id = gsp->stid.si_stateownerid;
	u32 f_id = gsp->stid.si_fileid;
	unsigned int hashval;

	dprintk("pNFSD: %s\n",__func__);

	dcp = find_pnfs_ds_clientid(&gsp->clid);
	if (!dcp)
		dcp = alloc_init_ds_clientid(gsp);
	if (!dcp)
		return NULL;

	dsp = kmalloc(sizeof(*dsp), GFP_KERNEL);
	if (!dsp)
		return dsp;

	INIT_LIST_HEAD(&dsp->ds_hash);
	INIT_LIST_HEAD(&dsp->ds_perclid);
	memcpy(&dsp->ds_stid, &gsp->stid, sizeof(stateid_t));
	memcpy(&dsp->ds_fh.fh_base, &cfh->fh_handle.fh_base,
			cfh->fh_handle.fh_size);
	dsp->ds_fh.fh_size =  cfh->fh_handle.fh_size;
	dsp->ds_access = gsp->access;
	dsp->ds_status = 0;
	dsp->ds_verifier[0] = gsp->verifier[0];
	dsp->ds_verifier[1] = gsp->verifier[1];

	list_add(&dsp->ds_perclid, &dcp->dc_stateid);

	hashval = stateid_hashval(st_id, f_id);
	list_add(&dsp->ds_hash, &ds_stid_hashtbl[hashval]);
	return dsp;
}

struct pnfs_ds_stateid *
nfsv4_ds_get_state(struct svc_fh *cfh, stateid_t *stidp)
{
	struct inode *ino = cfh->fh_dentry->d_inode;
	struct super_block *sb;
	struct pnfs_ds_stateid *dsp;
	struct pnfs_get_state gs = {
		.access = 0,
	};
	int status = 0;

	dprintk("pNFSD: %s\n",__func__);

	dsp = find_pnfs_ds_stateid(stidp);
	if(dsp)
		return dsp;
	memcpy(&gs.stid, stidp, sizeof(stateid_t));
	sb = ino->i_sb;
	if (sb && sb->s_export_op->get_state)
		status = sb->s_export_op->get_state(ino, &cfh->fh_handle, &gs);
		dprintk("pNFSD: %s from MDS status %d\n", __func__, status);
	if (status)
		return NULL;
	/* create new pnfs_ds_stateid */
	dsp = alloc_init_ds_stateid(cfh, &gs);
	return dsp;
}

int
nfs4_preprocess_pnfs_ds_stateid(struct svc_fh *cfh, stateid_t *stateid)
{
	struct pnfs_ds_stateid *dsp;

	/* BAD STATEID */
	dprintk("NFSD: nfs4_preprocess_pnfs_ds_stateid=(%08x/%08x/%08x/%08x)\n\n",
			stateid->si_boot,
			stateid->si_stateownerid,
			stateid->si_fileid,
			stateid->si_generation);

	dsp = nfsv4_ds_get_state(cfh, stateid);
	if (!dsp)
		return nfserr_bad_stateid;
	if ((cfh->fh_handle.fh_size != dsp->ds_fh.fh_size) ||
	    ((memcmp(&cfh->fh_handle.fh_base, &dsp->ds_fh.fh_base,
	                                dsp->ds_fh.fh_size)) != 0))
		return nfserr_bad_stateid;
	if (stateid->si_generation > dsp->ds_stid.si_generation)
		return nfserr_bad_stateid;

	/* OLD STATEID */
	if (stateid->si_generation < dsp->ds_stid.si_generation)
		return nfserr_old_stateid;
	return 0;
}
