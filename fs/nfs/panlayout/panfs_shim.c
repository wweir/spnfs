/*
 *  panfs_shim.c
 *
 *  Shim layer for interfacing with the Panasas DirectFlow module I/O stack
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

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include "panlayout.h"
#include "pnfs_osd_xdr.h"
#include "panfs_shim.h"

#include <linux/panfs_shim_api.h>

#define NFSDBG_FACILITY         NFSDBG_PNFS

struct panfs_export_operations *panfs_export_ops;

int
panfs_shim_ready(void)
{
	return panfs_export_ops != NULL;
}

static int
panfs_shim_conv_raid01(struct pnfs_osd_layout *layout,
		       struct pnfs_osd_data_map *lo_map,
		       pan_agg_layout_hdr_t *hdr)
{
	if (lo_map->odm_mirror_cnt) {
		hdr->type = PAN_AGG_RAID1;
		hdr->hdr.raid1.num_comps = lo_map->odm_mirror_cnt + 1;
	} else if (layout->olo_num_comps > 1) {
		hdr->type = PAN_AGG_RAID0;
		hdr->hdr.raid0.num_comps = layout->olo_num_comps;
		hdr->hdr.raid0.stripe_unit = lo_map->odm_stripe_unit;
	} else
		hdr->type = PAN_AGG_SIMPLE;
	return 0;
}

static int
panfs_shim_conv_raid5(struct pnfs_osd_layout *layout,
		      struct pnfs_osd_data_map *lo_map,
		      pan_agg_layout_hdr_t *hdr)
{
	if (lo_map->odm_mirror_cnt)
		goto err;

	if (lo_map->odm_group_width || lo_map->odm_group_depth) {
		if (!lo_map->odm_group_width || !lo_map->odm_group_depth)
			goto err;

		hdr->type = PAN_AGG_GRP_RAID5_LEFT;
		hdr->hdr.grp_raid5_left.num_comps = layout->olo_num_comps;
		if (hdr->hdr.grp_raid5_left.num_comps != layout->olo_num_comps)
			goto err;
		hdr->hdr.grp_raid5_left.stripe_unit = lo_map->odm_stripe_unit;
		hdr->hdr.grp_raid5_left.rg_width = lo_map->odm_group_width;
		hdr->hdr.grp_raid5_left.rg_depth = lo_map->odm_group_depth;
		/* this is a guess, panasas server is not supposed to
		   hand out layotu otherwise */
		hdr->hdr.grp_raid5_left.group_layout_policy =
			PAN_AGG_GRP_RAID5_LEFT_POLICY_ROUND_ROBIN;
	} else {
		hdr->type = PAN_AGG_RAID5_LEFT;
		hdr->hdr.raid5_left.num_comps = layout->olo_num_comps;
		if (hdr->hdr.raid5_left.num_comps != layout->olo_num_comps)
			goto err;
		hdr->hdr.raid5_left.stripe_unit2 =
		hdr->hdr.raid5_left.stripe_unit1 =
		hdr->hdr.raid5_left.stripe_unit0 = lo_map->odm_stripe_unit;
	}

	return 0;
err:
	return -EINVAL;
}

/*
 * Convert a pnfs_osd data map into Panasas aggregation layout header
 */
static int
panfs_shim_conv_pnfs_osd_data_map(
	struct pnfs_osd_layout *layout,
	pan_agg_layout_hdr_t *hdr)
{
	int status = -EINVAL;
	struct pnfs_osd_data_map *lo_map = &layout->olo_map;

	if (!layout->olo_num_comps)
		goto err;

	/* FIXME: need to handle maps describing only parity stripes */
	if (lo_map->odm_num_comps != layout->olo_num_comps)
		goto err;

	switch (lo_map->odm_raid_algorithm) {
	case PNFS_OSD_RAID_0:
		status = panfs_shim_conv_raid01(layout, lo_map, hdr);
		break;

	case PNFS_OSD_RAID_5:
		status = panfs_shim_conv_raid5(layout, lo_map, hdr);
		break;

	case PNFS_OSD_RAID_4:
	case PNFS_OSD_RAID_PQ:
	default:
		goto err;
	}

	return 0;

err:
	return status;
}

/*
 * Convert pnfs_osd layout into Panasas map and caps type
 */
int
panfs_shim_conv_layout(
	void **outp,
	struct pnfs_layout_segment *lseg,
	struct pnfs_osd_layout *layout)
{
	int i;
	int status;
	struct pnfs_osd_object_cred *lo_comp;
	pan_size_t alloc_sz, local_sz;
	pan_sm_map_cap_t *mcs = NULL;
	u8 *buf;
	pan_agg_comp_obj_t *pan_comp;
	pan_sm_sec_t *pan_sec;

	status = -EINVAL;
	alloc_sz = layout->olo_num_comps *
		   (sizeof(pan_agg_comp_obj_t) + sizeof(pan_sm_sec_t));
	for (i = 0; i < layout->olo_num_comps; i++) {
		void *p = layout->olo_comps[i].oc_cap->cred;
		if (panfs_export_ops->sm_sec_t_get_size_otw(
			(pan_sm_sec_otw_t *)&p, &local_sz, NULL, NULL))
			goto err;
		alloc_sz += local_sz;
	}

	status = -ENOMEM;
	mcs = kzalloc(sizeof(*mcs) + alloc_sz, GFP_KERNEL);
	if (!mcs)
		goto err;
	buf = (u8 *)&mcs[1];

	mcs->offset = lseg->range.offset;
	mcs->length = lseg->range.length;
#if 0
	/* FIXME: for now */
	mcs->expiration_time.ts_sec  = 0;
	mcs->expiration_time.ts_nsec = 0;
#endif
	mcs->full_map.map_hdr.avail_state = PAN_AGG_OBJ_STATE_NORMAL;
	status = panfs_shim_conv_pnfs_osd_data_map(layout,
						   &mcs->full_map.layout_hdr);
	if (status)
		goto err;

	mcs->full_map.components.size = layout->olo_num_comps;
	mcs->full_map.components.data = (pan_agg_comp_obj_t *)buf;
	buf += layout->olo_num_comps * sizeof(pan_agg_comp_obj_t);

	mcs->secs.size = layout->olo_num_comps;
	mcs->secs.data = (pan_sm_sec_t *)buf;
	buf += layout->olo_num_comps * sizeof(pan_sm_sec_t);

	lo_comp = layout->olo_comps;
	pan_comp = mcs->full_map.components.data;
	pan_sec = mcs->secs.data;
	for (i = 0; i < layout->olo_num_comps; i++) {
		void *p;
		pan_stor_obj_id_t *obj_id = &mcs->full_map.map_hdr.obj_id;
		struct pnfs_osd_objid *oc_obj_id = &lo_comp->oc_object_id;
		u64 dev_id = __be64_to_cpup(
			(__be64 *)oc_obj_id->oid_device_id.data + 1);

		dprintk("%s: i=%d deviceid=%Lx:%Lx partition=%Lx object=%Lx\n",
			__func__, i,
			__be64_to_cpup((__be64 *)oc_obj_id->oid_device_id.data),
			__be64_to_cpup((__be64 *)oc_obj_id->oid_device_id.data + 1),
			oc_obj_id->oid_partition_id, oc_obj_id->oid_object_id);

		if (i == 0) {
			/* make up mgr_id to calm sam down */
			pan_mgr_id_construct_artificial(PAN_MGR_SM, 0,
							&obj_id->dev_id);
			obj_id->grp_id = oc_obj_id->oid_partition_id;
			obj_id->obj_id = oc_obj_id->oid_object_id;
		}

		if (obj_id->grp_id != lo_comp->oc_object_id.oid_partition_id) {
			dprintk("%s: i=%d grp_id=0x%Lx oid_partition_id=0x%Lx\n",
				__func__, i, (u64)obj_id->grp_id,
				lo_comp->oc_object_id.oid_partition_id);
			status=-EINVAL;
			goto err;
		}

		if (obj_id->obj_id != lo_comp->oc_object_id.oid_object_id) {
			dprintk("%s: i=%d obj_id=0x%Lx oid_object_id=0x%Lx\n",
				__func__, i, obj_id->obj_id,
				lo_comp->oc_object_id.oid_object_id);
			status=-EINVAL;
			goto err;
		}

		pan_comp->dev_id = dev_id;
		if (!pan_stor_is_device_id_an_obsd_id(pan_comp->dev_id)) {
			dprintk("%s: i=%d dev_id=0x%Lx not an obsd_id\n",
				__func__, i, obj_id->dev_id);
			status=-EINVAL;
			goto err;
		}
		if (lo_comp->oc_osd_version == PNFS_OSD_MISSING) {
			dprintk("%s: degraded maps not supported yet\n",
				__func__);
			status=-ENOTSUPP;
			goto err;
		}
		pan_comp->avail_state = PAN_AGG_COMP_STATE_NORMAL;
		if (lo_comp->oc_cap_key_sec != PNFS_OSD_CAP_KEY_SEC_NONE) {
			dprintk("%s: cap key security not supported yet\n",
				__func__);
			status=-ENOTSUPP;
			goto err;
		}

		p = layout->olo_comps[i].oc_cap->cred;
		panfs_export_ops->sm_sec_t_unmarshall(
			(pan_sm_sec_otw_t *)&p,
			pan_sec,
			buf,
			alloc_sz,
			NULL,
			&local_sz);
		buf += local_sz;
		alloc_sz -= local_sz;

		lo_comp++;
		pan_comp++;
		pan_sec++;
	}

	*outp = mcs;
	dprintk("%s:Return mcs=%p\n", __func__, mcs);
	return 0;

err:
	panfs_shim_free_layout(mcs);
	dprintk("%s:Error %d\n", __func__, status);
	return status;
}

/*
 * Free a Panasas map and caps type
 */
void
panfs_shim_free_layout(void *p)
{
	kfree(p);
}

int
panfs_shim_register(struct panfs_export_operations *ops)
{
	if (panfs_export_ops) {
		printk(KERN_INFO
		       "%s: panfs already registered (panfs ops %p)\n",
		       __func__, panfs_export_ops);
		return -EINVAL;
	}

	printk(KERN_INFO "%s: registering panfs ops %p\n",
	       __func__, ops);

	panfs_export_ops = ops;
	return 0;
}
EXPORT_SYMBOL(panfs_shim_register);

int
panfs_shim_unregister(void)
{
	if (!panfs_export_ops) {
		printk(KERN_INFO "%s: panfs is not registered\n", __func__);
		return -EINVAL;
	}

	printk(KERN_INFO "%s: unregistering panfs ops %p\n",
	       __func__, panfs_export_ops);

	panfs_export_ops = NULL;
	return 0;
}
EXPORT_SYMBOL(panfs_shim_unregister);
