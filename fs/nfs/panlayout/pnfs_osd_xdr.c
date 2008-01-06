/*
 *  pnfs_osd_xdr.c
 *
 *  Object-Based pNFS Layout XDR layer
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

#include "pnfs_osd_xdr.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS

/*
 * The following implementation is based on these Internet Drafts:
 *
 * draft-ietf-nfsv4-minorversion-14
 * draft-ietf-nfsv4-pnfs-osd-04
 */

/* struct pnfs_osd_objid4 {
 *     deviceid4       device_id;
 *     uint64_t        partition_id;
 *     uint64_t        object_id;
 * }; */
static inline u32 *
pnfs_osd_xdr_decode_objid(u32 *p, struct pnfs_osd_objid *objid)
{
	READ64(objid->device_id);
	READ64(objid->partition_id);
	READ64(objid->object_id);
	return p;
}

static inline u32 *
pnfs_osd_xdr_decode_opaque_cred(u32 *p,
				struct pnfs_osd_opaque_cred *opaque_cred)
{
	READ32(opaque_cred->cred_len);
	COPYMEM(opaque_cred->cred, opaque_cred->cred_len);
	return p;
}

/* struct pnfs_osd_object_cred4 {
 *     pnfs_osd_objid4     object_id;
 *     pnfs_osd_version4   osd_version;
 *     opaque              credential<>;
 * }; */
static inline u32 *
pnfs_osd_xdr_decode_object_cred(u32 *p, struct pnfs_osd_object_cred *cred)
{
	p = pnfs_osd_xdr_decode_objid(p, &cred->object_id);
	READ32(cred->osd_version);
	return p;
}

/* struct pnfs_osd_data_map4 {
 *     length4                     stripe_unit;
 *     uint32_t                    group_width;
 *     uint32_t                    group_depth;
 *     uint32_t                    mirror_cnt;
 *     pnfs_osd_raid_algorithm4    raid_algorithm;
 * }; */
static inline u32 *
pnfs_osd_xdr_decode_data_map(u32 *p, struct pnfs_osd_data_map *data_map)
{
	READ64(data_map->stripe_unit);
	READ32(data_map->group_width);
	READ32(data_map->group_depth);
	READ32(data_map->mirror_cnt);
	READ32(data_map->raid_algorithm);
	return p;
}

struct pnfs_osd_layout *
pnfs_osd_xdr_decode_layout(struct pnfs_osd_layout *layout, u32 *p)
{
	int i;
	struct pnfs_osd_object_cred *comp;
	struct pnfs_osd_opaque_cred *cred;

	p = pnfs_osd_xdr_decode_data_map(p, &layout->map);
	READ32(layout->num_comps);
	layout->comps = (struct pnfs_osd_object_cred *)(layout + 1);
	comp = layout->comps;
	cred = (struct pnfs_osd_opaque_cred *)(comp + layout->num_comps);
	dprintk("%s: layout=%p num_comps=%u comps=%p\n", __func__,
	       layout, layout->num_comps, layout->comps);
	for (i = 0; i < layout->num_comps; i++) {
		comp->opaque_cred = cred;
		p = pnfs_osd_xdr_decode_object_cred(p, comp);
		p = pnfs_osd_xdr_decode_opaque_cred(p, cred);
		dprintk("%s: cred[%d]=%p cred_len=%u\n", __func__,
			i, cred, cred->cred_len);
		cred = (struct pnfs_osd_opaque_cred *)((u32 *)cred + 1 +
						XDR_QUADLEN(cred->cred_len));
		comp++;
	}
	dprintk("%s: end=%p count=%Zd\n", __func__,
	       cred, (char *)cred - (char *)layout);
	return layout;
}
