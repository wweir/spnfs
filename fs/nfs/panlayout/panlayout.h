/*
 *  panlayout.h
 *
 *  Data types and function declerations for interfacing with the
 *  panfs (Panasas DirectFlow) shim layer for the Panasas layout driver.
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

#ifndef _PANLAYOUT_H
#define _PANLAYOUT_H

/*@-oldstyle@*/
/*@-namechecks@*/
#include <linux/list.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/nfs_xdr.h>
#include <linux/nfs4_pnfs.h>
/*@=oldstyle@*/
/*@=namechecks@*/

#include "pnfs_osd_xdr.h"

#define PNFS_LAYOUT_PANOSD (NFS4_PNFS_PRIVATE_LAYOUT | LAYOUT_OSD2_OBJECTS)

/*
 * in-core layout segment
 */
struct panlayout_segment {
	void *panfs_internal;    /* Panasas internal */
	u8 pnfs_osd_layout[];
};

#if BITS_PER_LONG == 64
struct panlayout_atomic64 {
	atomic64_t val;
};

static inline void panlayout_atomic64_init(struct panlayout_atomic64 *p)
{
}

static inline s64 panlayout_atomic64_read(struct panlayout_atomic64 *p)
{
	return atomic64_read(&p->val);
}

static inline void panlayout_atomic64_set(struct panlayout_atomic64 *p, s64 val)
{
	atomic64_set(&p->val, val);
}

static inline void panlayout_atomic64_add(s64 val, struct panlayout_atomic64 *p)
{
	atomic64_add(val, &p->val);
}

static inline s64 panlayout_atomic64_xchg(struct panlayout_atomic64 *p, s64 val)
{
	return atomic64_xchg(&p->val, val);
}
#else  /* BITS_PER_LONG == 64 */
struct panlayout_atomic64 {
	spinlock_t lock;
	s64 val;
};

static inline void panlayout_atomic64_init(struct panlayout_atomic64 *p)
{
	spin_lock_init(&p->lock);
}

static inline s64 panlayout_atomic64_read(struct panlayout_atomic64 *p)
{
	s64 val;

	spin_lock(&p->lock);
	val = p->val;
	spin_unlock(&p->lock);
	return val;
}

static inline void panlayout_atomic64_set(struct panlayout_atomic64 *p, s64 val)
{
	spin_lock(&p->lock);
	p->val = val;
	spin_unlock(&p->lock);
}

static inline void panlayout_atomic64_add(s64 val, struct panlayout_atomic64 *p)
{
	spin_lock(&p->lock);
	p->val += val;
	spin_unlock(&p->lock);
}

static inline s64 panlayout_atomic64_xchg(struct panlayout_atomic64 *p, s64 val)
{
	s64 old;

	spin_lock(&p->lock);
	old = p->val;
	p->val = val;
	spin_unlock(&p->lock);
	return old;
}
#endif /* BITS_PER_LONG == 64 */

/*
 * per-inode layout
 */
struct panlayout {
	struct panlayout_atomic64 delta_space_used;  /* consumed by write ops */
};

/*
 * per-I/O operation state
 * embedded in shim layer io_state data structure
 */
struct panlayout_io_state {
	struct pnfs_layout_segment *lseg;
	void *rpcdata;
	int status;             /* res */
	int eof;                /* res */
	int committed;          /* res */
	s64 delta_space_used;   /* res */
};

/*
 * Panfs shim API
 */
extern int panfs_shim_ready(void);

extern int panfs_shim_conv_layout(
	void **outp,
	struct pnfs_layout_segment *lseg,
	struct pnfs_osd_layout *layout);
extern void panfs_shim_free_layout(void *p);

extern int panfs_shim_alloc_io_state(struct panlayout_io_state **outp);
extern void panfs_shim_free_io_state(struct panlayout_io_state *state);

extern void panfs_shim_iodone(struct panlayout_io_state *state);
extern ssize_t panfs_shim_read_pagelist(
	void *pl_state,
	struct page **pages,
	unsigned pgbase,
	unsigned nr_pages,
	loff_t offset,
	size_t count,
	int sync);

extern ssize_t panfs_shim_write_pagelist(
	void *pl_state,
	struct page **pages,
	unsigned pgbase,
	unsigned nr_pages,
	loff_t offset,
	size_t count,
	int sync,
	int stable);

/*
 * Panfs shim callback API
 */
extern void panlayout_read_done(struct panlayout_io_state *state);
extern void panlayout_write_done(struct panlayout_io_state *state);

#endif /* _PANLAYOUT_H */
