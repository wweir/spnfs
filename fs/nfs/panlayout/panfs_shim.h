/*
 *  panfs_shim.h
 *
 *  Data types and external function declerations for interfacing with
 *  panfs (Panasas DirectFlow) I/O stack
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

#ifndef _PANLAYOUT_PANFS_SHIM_H
#define _PANLAYOUT_PANFS_SHIM_H

typedef s8 pan_int8_t;
typedef u8 pan_uint8_t;
typedef s16 pan_int16_t;
typedef u16 pan_uint16_t;
typedef s32 pan_int32_t;
typedef u32 pan_uint32_t;
typedef s64 pan_int64_t;
typedef u64 pan_uint64_t;

/*
 * from pan_base_types.h
 */
typedef  pan_uint64_t pan_rpc_none_t;
typedef pan_uint32_t  pan_rpc_arrdim_t;
typedef pan_uint32_t  pan_status_t;
typedef pan_uint8_t   pan_otw_t;
typedef pan_uint8_t   pan_pad_t;

typedef pan_uint32_t  pan_timespec_sec_t;
typedef pan_uint32_t  pan_timespec_nsec_t;

typedef  struct pan_timespec_s  pan_timespec_t;
struct pan_timespec_s {
  pan_timespec_sec_t   ts_sec;
  pan_timespec_nsec_t  ts_nsec;
};

/*
 * from pan_std_types.h
 */
typedef pan_uint32_t pan_size_t;
typedef  int  pan_bool_t;

/*
 * from pan_common_error.h
 */
#define PAN_SUCCESS                                         ((pan_status_t)0)
#define PAN_ERR_IN_PROGRESS                                 ((pan_status_t)55)

/*
 * from pan_sg.h
 */
typedef struct pan_sg_entry_s pan_sg_entry_t;
struct pan_sg_entry_s {
  void                  *buffer;       /* pointer to memory */
  pan_uint32_t           chunk_size;   /* size of each chunk (bytes) */
  pan_sg_entry_t        *next;
};

/*
 * from pan_storage.h
 */
typedef pan_uint64_t pan_stor_dev_id_t;
typedef pan_uint32_t pan_stor_obj_grp_id_t;
typedef pan_uint64_t pan_stor_obj_uniq_t;
typedef pan_uint32_t pan_stor_action_t;
typedef pan_uint8_t pan_stor_cap_key_t[20];

typedef pan_uint8_t pan_stor_key_type_t;
typedef pan_uint64_t pan_stor_len_t;
typedef pan_int64_t pan_stor_delta_len_t;
typedef pan_uint64_t pan_stor_offset_t;
typedef pan_uint16_t pan_stor_op_t;

typedef pan_uint16_t pan_stor_sec_level_t;

struct pan_stor_obj_id_s {
  pan_stor_dev_id_t      dev_id;
  pan_stor_obj_uniq_t    obj_id;
  pan_stor_obj_grp_id_t  grp_id;
};

typedef struct pan_stor_obj_id_s pan_stor_obj_id_t;

#define PAN_STOR_OP_NONE ((pan_stor_op_t) 0U)
#define PAN_STOR_OP_READ ((pan_stor_op_t) 8U)
#define PAN_STOR_OP_WRITE ((pan_stor_op_t) 9U)
#define PAN_STOR_OP_APPEND ((pan_stor_op_t) 10U)
#define PAN_STOR_OP_GETATTR ((pan_stor_op_t) 11U)
#define PAN_STOR_OP_SETATTR ((pan_stor_op_t) 12U)
#define PAN_STOR_OP_FLUSH ((pan_stor_op_t) 13U)
#define PAN_STOR_OP_CLEAR ((pan_stor_op_t) 14U)

/*
 * from pan_aggregation_map.h
 */
typedef pan_uint8_t pan_agg_type_t;
typedef pan_uint64_t pan_agg_map_version_t;
typedef pan_uint8_t pan_agg_obj_state_t;
typedef pan_uint8_t pan_agg_comp_state_t;
typedef pan_uint8_t pan_agg_comp_flag_t;

#define PAN_AGG_OBJ_STATE_INVALID ((pan_agg_obj_state_t) 0x00)
#define PAN_AGG_OBJ_STATE_NORMAL ((pan_agg_obj_state_t) 0x01)
#define PAN_AGG_OBJ_STATE_DEGRADED ((pan_agg_obj_state_t) 0x02)
#define PAN_AGG_OBJ_STATE_RECONSTRUCT ((pan_agg_obj_state_t) 0x03)
#define PAN_AGG_OBJ_STATE_COPYBACK ((pan_agg_obj_state_t) 0x04)
#define PAN_AGG_OBJ_STATE_UNAVAILABLE ((pan_agg_obj_state_t) 0x05)
#define PAN_AGG_OBJ_STATE_CREATING ((pan_agg_obj_state_t) 0x06)
#define PAN_AGG_OBJ_STATE_DELETED ((pan_agg_obj_state_t) 0x07)
#define PAN_AGG_COMP_STATE_INVALID ((pan_agg_comp_state_t) 0x00)
#define PAN_AGG_COMP_STATE_NORMAL ((pan_agg_comp_state_t) 0x01)
#define PAN_AGG_COMP_STATE_UNAVAILABLE ((pan_agg_comp_state_t) 0x02)
#define PAN_AGG_COMP_STATE_COPYBACK ((pan_agg_comp_state_t) 0x03)
#define PAN_AGG_COMP_F_NONE ((pan_agg_comp_flag_t) 0x00)
#define PAN_AGG_COMP_F_ATTR_STORING ((pan_agg_comp_flag_t) 0x01)
#define PAN_AGG_COMP_F_OBJ_CORRUPT_OBS ((pan_agg_comp_flag_t) 0x02)
#define PAN_AGG_COMP_F_TEMP ((pan_agg_comp_flag_t) 0x04)

struct pan_aggregation_map_s {
  pan_agg_map_version_t  version;
  pan_agg_obj_state_t    avail_state;
  pan_stor_obj_id_t      obj_id;
};

typedef struct pan_aggregation_map_s pan_aggregation_map_t;

struct pan_agg_comp_obj_s {
  pan_stor_dev_id_t     dev_id;
  pan_agg_comp_state_t  avail_state;
  pan_agg_comp_flag_t   comp_flags;
};

typedef struct pan_agg_comp_obj_s pan_agg_comp_obj_t;

struct pan_agg_simple_header_s {
  pan_uint8_t  unused;
};

typedef struct pan_agg_simple_header_s pan_agg_simple_header_t;

struct pan_agg_raid1_header_s {
  pan_uint16_t  num_comps;
};

typedef struct pan_agg_raid1_header_s pan_agg_raid1_header_t;

struct pan_agg_raid0_header_s {
  pan_uint16_t  num_comps;
  pan_uint32_t  stripe_unit;
};

typedef struct pan_agg_raid0_header_s pan_agg_raid0_header_t;

struct pan_agg_raid5_left_header_s {
  pan_uint16_t  num_comps;
  pan_uint32_t  stripe_unit0;
  pan_uint32_t  stripe_unit1;
  pan_uint32_t  stripe_unit2;
};

typedef struct pan_agg_raid5_left_header_s pan_agg_raid5_left_header_t;

typedef struct pan_agg_grp_raid5_left_header_s pan_agg_grp_raid5_left_header_t;

struct pan_agg_grp_raid5_left_header_s {
  pan_uint16_t  num_comps;
  pan_uint32_t  stripe_unit;
  pan_uint16_t  rg_width;
  pan_uint16_t  rg_depth;
  pan_uint8_t   group_layout_policy;
};

#define PAN_AGG_GRP_RAID5_LEFT_POLICY_INVALID ((pan_uint8_t) 0x00)
#define PAN_AGG_GRP_RAID5_LEFT_POLICY_ROUND_ROBIN ((pan_uint8_t) 0x01)

#define PAN_AGG_NULL_MAP ((pan_agg_type_t) 0x00)
#define PAN_AGG_SIMPLE ((pan_agg_type_t) 0x01)
#define PAN_AGG_RAID1 ((pan_agg_type_t) 0x02)
#define PAN_AGG_RAID0 ((pan_agg_type_t) 0x03)
#define PAN_AGG_RAID5_LEFT ((pan_agg_type_t) 0x04)
#define PAN_AGG_GRP_RAID5_LEFT ((pan_agg_type_t) 0x06)
#define PAN_AGG_MINTYPE ((pan_agg_type_t) 0x01)
#define PAN_AGG_MAXTYPE ((pan_agg_type_t) 0x06)

struct pan_agg_layout_hdr_s {
  pan_agg_type_t type;
  pan_pad_t pad[3];
  union {
    pan_uint64_t                        null;
    pan_agg_simple_header_t             simple;
    pan_agg_raid1_header_t              raid1;
    pan_agg_raid0_header_t              raid0;
    pan_agg_raid5_left_header_t         raid5_left;
    pan_agg_grp_raid5_left_header_t     grp_raid5_left;
  } hdr;
};

typedef struct pan_agg_layout_hdr_s pan_agg_layout_hdr_t;

struct pan_agg_comp_obj_a_s {
  pan_rpc_arrdim_t size;
  pan_agg_comp_obj_t * data;
};
typedef struct pan_agg_comp_obj_a_s pan_agg_comp_obj_a;

struct pan_agg_full_map_s {
  pan_aggregation_map_t  map_hdr;
  pan_agg_layout_hdr_t   layout_hdr;
  pan_agg_comp_obj_a     components;
};

typedef struct pan_agg_full_map_s pan_agg_full_map_t;

/*
 * from pan_obsd_rpc_types.h
 */
typedef pan_uint8_t pan_obsd_security_key_a[16];

typedef pan_uint8_t pan_obsd_capability_key_a[20];

typedef pan_uint8_t pan_obsd_key_holder_id_t;

#define PAN_OBSD_KEY_HOLDER_BASIS_KEY ((pan_obsd_key_holder_id_t) 0x01)
#define PAN_OBSD_KEY_HOLDER_CAP_KEY ((pan_obsd_key_holder_id_t) 0x02)

struct pan_obsd_key_holder_s {
  pan_obsd_key_holder_id_t select;
  pan_pad_t pad[3];
  union {
    pan_obsd_security_key_a    basis_key;
    pan_obsd_capability_key_a  cap_key;
  } key;
};

typedef struct pan_obsd_key_holder_s pan_obsd_key_holder_t;

/*
 * from pan_sm_sec.h
 */
typedef pan_uint8_t pan_sm_sec_type_t;
typedef pan_uint8_t pan_sm_sec_otw_allo_mode_t;

struct pan_obsd_capability_generic_otw_t_s {
  pan_rpc_arrdim_t size;
  pan_uint8_t * data;
};
typedef struct pan_obsd_capability_generic_otw_t_s pan_obsd_capability_generic_otw_t;

struct pan_sm_sec_obsd_s {
  pan_obsd_key_holder_t              key;
  pan_obsd_capability_generic_otw_t  cap_otw;
  pan_sm_sec_otw_allo_mode_t         allo_mode;
};

typedef struct pan_sm_sec_obsd_s pan_sm_sec_obsd_t;

struct pan_sm_sec_s {
  pan_sm_sec_type_t type;
  pan_pad_t pad[3];
  union {
    pan_rpc_none_t     none;
    pan_sm_sec_obsd_t  obsd;
  } variant;
};

typedef struct pan_sm_sec_s pan_sm_sec_t;

struct pan_sm_sec_a_s {
  pan_rpc_arrdim_t size;
  pan_sm_sec_t * data;
};
typedef struct pan_sm_sec_a_s pan_sm_sec_a;
typedef pan_otw_t *pan_sm_sec_otw_t;

/*
 * from pan_sm_types.h
 */
typedef pan_uint64_t pan_sm_cap_handle_t;

struct pan_sm_map_cap_s {
  pan_agg_full_map_t   full_map;
  pan_stor_offset_t    offset;
  pan_stor_len_t       length;
  pan_sm_sec_a         secs;
  pan_sm_cap_handle_t  handle;
  pan_timespec_t       expiration_time;
  pan_stor_action_t    action_mask;
  pan_uint32_t         flags;
};

typedef struct pan_sm_map_cap_s pan_sm_map_cap_t;

/*
 * from pan_sm_ops.h
 */
typedef pan_rpc_none_t pan_sm_cache_ptr_t;

/*
 * from pan_sam_api.h
 */
typedef pan_uint32_t    pan_sam_access_flags_t;

typedef struct pan_sam_dev_error_s  pan_sam_dev_error_t;
struct pan_sam_dev_error_s {
    pan_stor_dev_id_t       dev_id;
    pan_stor_op_t           stor_op;
    pan_status_t            error;
};

typedef struct pan_sam_ext_status_s pan_sam_ext_status_t;
struct pan_sam_ext_status_s {
    pan_uint32_t        available;
    pan_uint32_t        size;
    pan_sam_dev_error_t *errors;
};

enum pan_sam_rpc_sec_sel_e {
    PAN_SAM_RPC_SEC_DEFAULT,
    PAN_SAM_RPC_SEC_ATLEAST,
    PAN_SAM_RPC_SEC_EXACTLY
};
typedef enum pan_sam_rpc_sec_sel_e pan_sam_rpc_sec_sel_t;

typedef struct pan_sam_obj_sec_s pan_sam_obj_sec_t;
struct pan_sam_obj_sec_s {
    pan_stor_sec_level_t    min_security;
    pan_sm_map_cap_t        *map_ccaps;
};

typedef struct  pan_sam_rpc_sec_s   pan_sam_rpc_sec_t;
struct pan_sam_rpc_sec_s {
    pan_sam_rpc_sec_sel_t   selector;
};

typedef struct pan_sam_read_args_s pan_sam_read_args_t;
struct pan_sam_read_args_s {
    pan_stor_obj_id_t                obj_id;
    pan_sm_cache_ptr_t               obj_ent;
    void                            *return_attr;
    void                            *checksum;
    pan_stor_offset_t                offset;
    pan_uint16_t                     sm_options;
    void                            *callout;
    void                            *callout_arg;
};

typedef struct pan_sam_read_res_s pan_sam_read_res_t;
struct pan_sam_read_res_s {
    pan_status_t             result;
    pan_sam_ext_status_t     ext_status;
    pan_stor_len_t           length;
    void                    *attr;
    void                    *checksum;
};

typedef void (*pan_sam_read_cb_t)(
    void                *user_arg1,
    void                *user_arg2,
    pan_sam_read_res_t  *res_p,
    pan_status_t        status);

#define PAN_SAM_ACCESS_NONE                             0x0000
#define PAN_SAM_ACCESS_BYPASS_TIMESTAMP                 0x0020

typedef struct pan_sam_write_args_s pan_sam_write_args_t;
struct pan_sam_write_args_s {
    pan_stor_obj_id_t   obj_id;
    pan_sm_cache_ptr_t  obj_ent;
    pan_stor_offset_t   offset;
    void                *attr;
    void                *return_attr;
};

typedef struct pan_sam_write_res_s pan_sam_write_res_t;
struct pan_sam_write_res_s {
    pan_status_t            result;
    pan_sam_ext_status_t    ext_status;
    pan_stor_len_t          length;
    pan_stor_delta_len_t    delta_capacity_used;
    pan_bool_t              parity_dirty;
    void                   *attr;
};

typedef void (*pan_sam_write_cb_t)(
    void                *user_arg1,
    void                *user_arg2,
    pan_sam_write_res_t *res_p,
    pan_status_t        status);

/*
 * from pan_mgr_types.h
 */
#define PAN_MGR_ID_TYPE_SHIFT 56
#define PAN_MGR_ID_TYPE_MASK ((pan_mgr_id_t)18374686479671623680ULL)
#define PAN_MGR_ID_UNIQ_MASK ((pan_mgr_id_t)72057594037927935ULL)

typedef pan_uint16_t pan_mgr_type_t;
typedef pan_uint64_t pan_mgr_id_t;

#define PAN_MGR_SM ((pan_mgr_type_t) 2U)
#define PAN_MGR_OBSD ((pan_mgr_type_t) 6U)

/*
 * from pan_mgr_types_c.h
 */
#define pan_mgr_id_construct_artificial(_mgr_type_,_mgr_uniq_,_mgr_id_p_) { \
  pan_mgr_id_t  _id1, _id2; \
\
  _id1 = (_mgr_type_); \
  _id1 <<= PAN_MGR_ID_TYPE_SHIFT; \
  _id1 &= PAN_MGR_ID_TYPE_MASK; \
  _id2 = (_mgr_uniq_); \
  _id2 &= PAN_MGR_ID_UNIQ_MASK; \
  _id1 |= _id2; \
  *(_mgr_id_p_) = _id1; \
}

/*
 * from pan_storage_c.h
 */
#define pan_stor_is_device_id_an_obsd_id(_device_id_) \
    ((((_device_id_) & PAN_MGR_ID_TYPE_MASK) >> PAN_MGR_ID_TYPE_SHIFT) == PAN_MGR_OBSD)

/*
 * pnfs_shim internal definitions
 */

struct panfs_shim_io_state {
	struct panlayout_io_state pl_state;

	pan_sg_entry_t *sg_list;
	struct page **pages;
	unsigned nr_pages;
	pan_sam_obj_sec_t obj_sec;
	void *ucreds;
	union {
		struct {
			pan_sam_read_args_t args;
			pan_sam_read_res_t res;
		} read;
		struct {
			pan_sam_write_args_t args;
			pan_sam_write_res_t res;
		} write;
	} u;
};

#endif /* _PANLAYOUT_PANFS_SHIM_H */
