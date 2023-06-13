// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2023 Datto Inc.

#ifndef FTRACE_HOOKING_H_INCLUDE
#define FTRACE_HOOKING_H_INCLUDE

#include <linux/mount.h>
#include <linux/version.h>
#include "tracer.h"
#include "includes.h"
#include "logging.h"
#include "bdev_state_handler.h"


#ifdef HAVE_UAPI_MOUNT_H
#include <uapi/linux/mount.h>
#endif

struct ftrace_hook {
	const char *name;
	void *function;
	void *original;

	unsigned long address;
	struct ftrace_ops ops;
};

#define HOOK(_name, _function, _original)	\
	{										\
		.name = (_name),					\
		.function = (_function),			\
		.original = (_original),			\
	}

#define USE_FENTRY_OFFSET 0

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
#define FTRACE_OPS_FL_RECURSION FTRACE_OPS_FL_RECURSION_SAFE
#define ftrace_regs pt_regs

static __always_inline struct pt_regs *ftrace_get_regs(struct ftrace_regs *fregs)
{
	return fregs;
}
#endif

#ifndef UMOUNT_NOFOLLOW
#define UMOUNT_NOFOLLOW 0
#endif

#define handle_bdev_mount_nowrite(dir_name, follow_flags, idx_out)             \
        handle_bdev_mount_event(dir_name, follow_flags, idx_out, 0)
#define handle_bdev_mounted_writable(dir_name, idx_out)                        \
        handle_bdev_mount_event(dir_name, 0, idx_out, 1)


#ifdef HAVE_SYS_OLDUMOUNT
static asmlinkage long (*orig_oldumount)(char __user *);
#endif


int register_ftrace_hooks(void);
int unregister_ftrace_hooks(void);


#endif //FTRACE_HOOKING_H_INCLUDE


