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
 * draft-ietf-nfsv4-minorversion-14
 * draft-ietf-nfsv4-pnfs-osd-04
 */

enum pnfs_obj_addr_type {
	OBJ_TARGET_NETADDR            = 1,
	OBJ_TARGET_IQN                = 2,
	OBJ_TARGET_WWN                = 3
};


/*   struct netaddr4 {
 *       // see struct rpcb in RFC1833
 *       string r_netid<>;    // network id
 *       string r_addr<>;     // universal address
 *   }; */
struct pnfs_osd_net_addr {
	struct nfs4_string            r_netid;
	struct nfs4_string            r_addr;
};

/*   struct pnfs_osd_deviceaddr4 {
 *       union target switch (pnfs_osd_addr_type4 type) {
 *           case OBJ_TARGET_NETADDR:
 *               pnfs_netaddr4   netaddr;
 *
 *           case OBJ_TARGET_IQN:
 *               string          iqn<>;
 *
 *           case OBJ_TARGET_WWN:
 *               string          wwn<>;
 *
 *           default:
 *               void;
 *       };
 *       uint64_t            lun;
 *       opaque              systemid<>;
 *       opaque              osdname<>;
 *   }; */
struct pnfs_osd_deviceaddr {
	u32                           type;
	union {
		struct pnfs_osd_net_addr  netaddr;
		struct nfs4_string        iqn;
		struct nfs4_string        wwn;
	} u;
	u64                           lun;
	struct nfs4_string            systemid;
	struct nfs4_string            osdname;
};

enum pnfs_osd_raid_algorithm4 {
	PNFS_OSD_RAID_0               = 1,
	PNFS_OSD_RAID_4               = 2,
	PNFS_OSD_RAID_5               = 3,
	PNFS_OSD_RAID_PQ              = 4     /* Reed-Solomon P+Q */
};

/*   struct pnfs_osd_data_map4 {
 *       length4                     stripe_unit;
 *       uint32_t                    group_width;
 *       uint32_t                    group_depth;
 *       uint32_t                    mirror_cnt;
 *       pnfs_osd_raid_algorithm4    raid_algorithm;
 *   }; */
struct pnfs_osd_data_map {
	u64                           stripe_unit;
	u32                           group_width;
	u32                           group_depth;
	u32                           mirror_cnt;
	u32                           raid_algorithm;
};

static inline size_t
pnfs_osd_data_map_xdr_sz(u32 *p)
{
	return 2 + 1 + 1 + 1 + 1;
}

static inline size_t
pnfs_osd_data_map_incore_sz(u32 *p)
{
	return sizeof(struct pnfs_osd_data_map);
}

/*   struct pnfs_osd_objid4 {
 *       deviceid4       device_id;
 *       uint64_t        partition_id;
 *       uint64_t        object_id;
 *   }; */
struct pnfs_osd_objid {
	u64                           device_id;
	u64                           partition_id;
	u64                           object_id;
};

static inline size_t
pnfs_osd_objid_xdr_sz(u32 *p)
{
	return 3 * 2;
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
 *       pnfs_osd_objid4         object_id;
 *       pnfs_osd_version4       osd_version;
 *       pnfs_osd_cap_key_sec4   cap_key_sec;
 *       opaque                  capability_key<>;
 *       opaque                  capability<>;
 *   }; */
struct pnfs_osd_object_cred {
	struct pnfs_osd_objid         object_id;
	u32                           osd_version;
	struct pnfs_osd_opaque_cred *opaque_cred;
#if 0
/* FIXME: break the credential into cap_key and cap as per draft-04 */
	u32                           cap_key_sec;
	struct nfs4_string            cap_key;
	struct nfs4_string            cap;
#endif
};

static inline size_t
pnfs_osd_object_cred_xdr_sz(u32 *p)
{
	u32 *start = p;

	p += pnfs_osd_objid_xdr_sz(p) + 1;
	p += pnfs_osd_opaque_cred_xdr_sz(p);
	return p - start;
}

static inline size_t
pnfs_osd_object_cred_incore_sz(u32 *p)
{
	p += pnfs_osd_objid_xdr_sz(p) + 1;
	return sizeof(struct pnfs_osd_object_cred) +
	       pnfs_osd_opaque_cred_incore_sz(p);
}

/*   struct pnfs_osd_layout4 {
 *       pnfs_osd_data_map4      map;
 *       pnfs_osd_object_cred4   components<>;
 *   }; */
struct pnfs_osd_layout {
	struct pnfs_osd_data_map      map;
	u32                           num_comps;
	struct pnfs_osd_object_cred  *comps;
};

/*   struct pnfs_osd_layoutupdate4 {
 *       pnfs_osd_deltaspaceused4    delta_space_used;
 *       pnfs_osd_ioerr4             ioerr<>;
 *   }; */
struct nfs4_panlayout_update {
	u32                           delta_space_valid;
	s64                           delta_space_used;
/* FIXME: implement ioerr */
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

extern struct pnfs_osd_layout *pnfs_osd_xdr_decode_layout(
	struct pnfs_osd_layout *layout, u32 *p);

#endif /* _PANLAYOUT_PNFS_OSD_XDR_H */
