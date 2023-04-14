/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#ifndef ELASTIO_SNAP_INCLUDES_H_
#define ELASTIO_SNAP_INCLUDES_H_

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>
// 'genhd.h' has been removed and 'gendisk' struct has been moved to the 'blkdev.h' in the kernel 5.18.
// Old compilers may not have '__has_include' macro, but 'genhd.h' exists on those systems.
#if defined __has_include
# if __has_include (<linux/genhd.h>)
#  include <linux/genhd.h>
# endif
#else
# include <linux/genhd.h>
#endif
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/buffer_head.h>
#include <linux/namei.h>
#include <linux/unistd.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <asm/div64.h>

#endif
