/*
 *  pnfs_osd_xdr.h
 *
 *  pNFS-osd on-the-wire data structures
 *
 *  Copyright (C) 2007 Panasas Inc.
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
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
 *  3. Neither the name of the Panasas company nor the names of its
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
 * See the file COPYING included with this distribution for more details.
 *
 */
#ifndef _PANLAYOUT_PNFS_OSD_XDR_H
#define _PANLAYOUT_PNFS_OSD_XDR_H

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/nfs_xdr.h>

#define PNFS_OSD_SYSTEMID_MAXSIZE 256
#define PNFS_OSD_OSDNAME_MAXSIZE 256

/*
 * START OF "GENERIC" DECODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

/*
 * draft-ietf-nfsv4-minorversion-22
 * draft-ietf-nfsv4-pnfs-osd-07
 */

/* Layout Structure */

enum pnfs_osd_raid_algorithm4 {
	PNFS_OSD_RAID_0               = 1,
	PNFS_OSD_RAID_4               = 2,
	PNFS_OSD_RAID_5               = 3,
	PNFS_OSD_RAID_PQ              = 4     /* Reed-Solomon P+Q */
};

/*   struct pnfs_osd_data_map4 {
 *       uint32_t                    odm_num_comps;
 *       length4                     odm_stripe_unit;
 *       uint32_t                    odm_group_width;
 *       uint32_t                    odm_group_depth;
 *       uint32_t                    odm_mirror_cnt;
 *       pnfs_osd_raid_algorithm4    odm_raid_algorithm;
 *   };
 */
struct pnfs_osd_data_map {
	u32				odm_num_comps;
	u64				odm_stripe_unit;
	u32				odm_group_width;
	u32				odm_group_depth;
	u32				odm_mirror_cnt;
	u32				odm_raid_algorithm;
};

static inline size_t
pnfs_osd_data_map_xdr_sz(u32 *p)
{
	return 1 + 2 + 1 + 1 + 1 + 1;
}

static inline size_t
pnfs_osd_data_map_incore_sz(u32 *p)
{
	return sizeof(struct pnfs_osd_data_map);
}

/*   struct pnfs_osd_objid4 {
 *       deviceid4       oid_device_id;
 *       uint64_t        oid_partition_id;
 *       uint64_t        oid_object_id;
 *   };
 */
struct pnfs_osd_objid {
	struct pnfs_deviceid		oid_device_id;
	u64				oid_partition_id;
	u64				oid_object_id;
};

static inline size_t
pnfs_osd_objid_xdr_sz(u32 *p)
{
	return (NFS4_PNFS_DEVICEID4_SIZE / 4) + 2 + 2;
}

static inline size_t
pnfs_osd_objid_incore_sz(u32 *p)
{
	return sizeof(struct pnfs_osd_objid);
}

enum pnfs_osd_version {
	PNFS_OSD_MISSING              = 0,
	PNFS_OSD_VERSION_1            = 1,
	PNFS_OSD_VERSION_2            = 2
};

struct pnfs_osd_opaque_cred {
	u32 cred_len;
	char cred[];
};

static inline size_t
pnfs_osd_opaque_cred_xdr_sz(u32 *p)
{
	u32 *start = p;
	u32 n;

	READ32(n);
	p += XDR_QUADLEN(n);
	return p - start;
}

static inline size_t
pnfs_osd_opaque_cred_incore_sz(u32 *p)
{
	u32 n;

	READ32(n);
	return sizeof(struct pnfs_osd_opaque_cred) + XDR_QUADLEN(n) * 4;
}

enum pnfs_osd_cap_key_sec {
	PNFS_OSD_CAP_KEY_SEC_NONE     = 0,
	PNFS_OSD_CAP_KEY_SEC_SSV      = 1,
};

/*   struct pnfs_osd_object_cred4 {
 *       pnfs_osd_objid4         oc_object_id;
 *       pnfs_osd_version4       oc_osd_version;
 *       pnfs_osd_cap_key_sec4   oc_cap_key_sec;
 *       opaque                  oc_capability_key<>;
 *       opaque                  oc_capability<>;
 *   };
 */
struct pnfs_osd_object_cred {
	struct pnfs_osd_objid		oc_object_id;
	u32				oc_osd_version;
	u32				oc_cap_key_sec;
	struct pnfs_osd_opaque_cred	*oc_cap_key;
	struct pnfs_osd_opaque_cred	*oc_cap;
};

static inline size_t
pnfs_osd_object_cred_xdr_sz(u32 *p)
{
	u32 *start = p;

	p += pnfs_osd_objid_xdr_sz(p) + 2;
	p += pnfs_osd_opaque_cred_xdr_sz(p);
	p += pnfs_osd_opaque_cred_xdr_sz(p);
	return p - start;
}

static inline size_t
pnfs_osd_object_cred_incore_sz(u32 *p)
{
	p += pnfs_osd_objid_xdr_sz(p) + 2;
	return sizeof(struct pnfs_osd_object_cred) +
	       pnfs_osd_opaque_cred_incore_sz(p) +
	       pnfs_osd_opaque_cred_incore_sz(p);
}

/*   struct pnfs_osd_layout4 {
 *       pnfs_osd_data_map4      olo_map;
 *       uint32_t                olo_comps_index;
 *       pnfs_osd_object_cred4   olo_components<>;
 *   };
 */
struct pnfs_osd_layout {
	struct pnfs_osd_data_map	olo_map;
	u32				olo_comps_index;
	u32				olo_num_comps;
	struct pnfs_osd_object_cred	*olo_comps;
};

static inline size_t
pnfs_osd_layout_xdr_sz(u32 *p)
{
	u32 *start = p;
	u32 n;

	p += pnfs_osd_data_map_xdr_sz(p);
	READ32(n);
	while ((int)(n--) > 0)
		p += pnfs_osd_object_cred_xdr_sz(p);
	return p - start;
}

static inline size_t
pnfs_osd_layout_incore_sz(u32 *p)
{
	u32 n;
	size_t sz;

	p += pnfs_osd_data_map_xdr_sz(p);
	READ32(n);
	sz = sizeof(struct pnfs_osd_layout);
	while ((int)(n--) > 0) {
		sz += pnfs_osd_object_cred_incore_sz(p);
		p += pnfs_osd_object_cred_xdr_sz(p);
	}
	return sz;
}

/* Device Address */

enum pnfs_osd_targetid_type {
	OBJ_TARGET_ANON = 1,
	OBJ_TARGET_SCSI_NAME = 2,
	OBJ_TARGET_SCSI_DEVICE_ID = 3,
};

/*   union pnfs_osd_targetid4 switch (pnfs_osd_targetid_type4 oti_type) {
 *       case OBJ_TARGET_SCSI_NAME:
 *           string              oti_scsi_name<>;
 *
 *       case OBJ_TARGET_SCSI_DEVICE_ID:
 *           opaque              oti_scsi_device_id<>;
 *
 *       default:
 *           void;
 *   };
 *
 *   union pnfs_osd_targetaddr4 switch (bool ota_available) {
 *       case TRUE:
 *           netaddr4            ota_netaddr;
 *       case FALSE:
 *           void;
 *   };
 *
 *   struct pnfs_osd_deviceaddr4 {
 *       pnfs_osd_targetid4      oda_targetid;
 *       pnfs_osd_targetaddr4    oda_targetaddr;
 *       uint64_t                oda_lun;
 *       opaque                  oda_systemid<>;
 *       pnfs_osd_object_cred4   oda_root_obj_cred;
 *       opaque                  oda_osdname<>;
 *   };
 */
struct pnfs_osd_targetid {
	u32				oti_type;
	struct nfs4_string		oti_scsi_device_id;
};

/*   struct netaddr4 {
 *       // see struct rpcb in RFC1833
 *       string r_netid<>;    // network id
 *       string r_addr<>;     // universal address
 *   }; */
struct pnfs_osd_net_addr {
	struct nfs4_string		r_netid;
	struct nfs4_string		r_addr;
};

struct pnfs_osd_targetaddr {
	u32				ota_available;
	struct pnfs_osd_net_addr	ota_netaddr;
};

struct pnfs_osd_deviceaddr {
	struct pnfs_osd_targetid	oda_targetid;
	struct pnfs_osd_targetaddr	oda_targetaddr;
	u8				oda_lun[8];
	struct nfs4_string		oda_systemid;
	struct pnfs_osd_object_cred	oda_root_obj_cred;
	struct nfs4_string		oda_osdname;
};

/* LAYOUTCOMMIT: layoutupdate */

/*   union pnfs_osd_deltaspaceused4 switch (bool dsu_valid) {
 *       case TRUE:
 *           int64_t     dsu_delta;
 *       case FALSE:
 *           void;
 *   };
 *
 *   struct pnfs_osd_layoutupdate4 {
 *       pnfs_osd_deltaspaceused4    olu_delta_space_used;
 *       bool                        olu_ioerr_flag;
 *   };
 */
struct nfs4_panlayout_update {
	u32				dsu_valid;
	s64				dsu_delta;
	u32				olu_ioerr_flag;
};

/* LAYOUTRETURN: I/O Rrror Report */

enum pnfs_osd_errno {
	PNFS_OSD_ERR_EIO		= 1,
	PNFS_OSD_ERR_NOT_FOUND		= 2,
	PNFS_OSD_ERR_NO_SPACE		= 3,
	PNFS_OSD_ERR_BAD_CRED		= 4,
	PNFS_OSD_ERR_NO_ACCESS		= 5,
	PNFS_OSD_ERR_UNREACHABLE	= 6,
	PNFS_OSD_ERR_RESOURCE		= 7
};

/*   struct pnfs_osd_ioerr4 {
 *       pnfs_osd_objid4     oer_component;
 *       length4             oer_comp_offset;
 *       length4             oer_comp_length;
 *       bool                oer_iswrite;
 *       pnfs_osd_errno4     oer_errno;
 *   };
 */
struct pnfs_osd_ioerr {
    struct pnfs_osd_objid		oer_component;
    u64					oer_comp_offset;
    u64					oer_comp_length;
    u32					oer_iswrite;
    u32					oer_errno;
};

extern struct pnfs_osd_layout *pnfs_osd_xdr_decode_layout(
	struct pnfs_osd_layout *layout, u32 *p);

#endif /* _PANLAYOUT_PNFS_OSD_XDR_H */
