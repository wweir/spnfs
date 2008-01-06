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
