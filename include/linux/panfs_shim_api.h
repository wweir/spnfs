#ifndef _PANFS_SHIM_API_H
#define _PANFS_SHIM_API_H

/*
 * imported panfs functions
 */
struct panfs_export_operations {
	int (*convert_rc)(pan_status_t rc);

	int (*sm_sec_t_get_size_otw)(
		pan_sm_sec_otw_t *var,
		pan_size_t *core_sizep,
		pan_size_t *wire_size,
		void *buf_end);

	int (*sm_sec_t_unmarshall)(
		pan_sm_sec_otw_t *in,
		pan_sm_sec_t *out,
		void *buf,
		pan_size_t size,
		pan_size_t *otw_consumed,
		pan_size_t *in_core_consumed);

	int (*ucreds_get)(void **ucreds_pp);

	void (*ucreds_put)(void *ucreds);

	int (*sam_read)(
		pan_sam_access_flags_t  flags,
		pan_sam_read_args_t    *args_p,
		pan_sam_obj_sec_t      *obj_sec_p,
		pan_sg_entry_t         *data_p,
		void                   *ucreds,
		pan_sam_read_cb_t       closure,
		void                   *user_arg1,
		void                   *user_arg2,
		pan_sam_read_res_t     *res_p);

	int (*sam_write)(
		pan_sam_access_flags_t  flags,
		pan_sam_write_args_t   *args_p,
		pan_sam_obj_sec_t      *obj_sec_p,
		pan_sg_entry_t         *data_p,
		void                   *ucreds,
		pan_sam_write_cb_t      closure,
		void                   *user_arg1,
		void                   *user_arg2,
		pan_sam_write_res_t    *res_p);
};

extern int
panfs_shim_register(struct panfs_export_operations *ops);

extern int
panfs_shim_unregister(void);

#endif /* _PANFS_SHIM_API_H */
