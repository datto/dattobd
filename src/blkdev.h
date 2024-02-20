// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef BLKDEV_H_
#define BLKDEV_H_

#include "includes.h"

struct block_device;

#ifdef HAVE_BLKDEV_PUT_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
#define dattobd_blkdev_put(bdev) blkdev_put(bdev);
#else
#define dattobd_blkdev_put(bdev) blkdev_put(bdev, FMODE_READ);
#endif

#ifndef bdev_whole
//#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
#define bdev_whole(bdev) ((bdev)->bd_contains)
#endif

#ifndef HAVE_HD_STRUCT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
#define dattobd_bdev_size(bdev) (bdev_nr_sectors(bdev))
#elif !defined HAVE_PART_NR_SECTS_READ
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
#define dattobd_bdev_size(bdev) ((bdev)->bd_part->nr_sects)
#else
#define dattobd_bdev_size(bdev) part_nr_sects_read((bdev)->bd_part)
#endif

#if !defined HAVE_BLKDEV_GET_BY_PATH_1
#define dattobd_blkdev_get_by_path(path, mode, holder) blkdev_get_by_path(path,mode,holder)
#else
#define dattobd_blkdev_get_by_path(path, mode, holder) blkdev_get_by_path(path,mode,holder,NULL)
#endif 

#if !defined HAVE_BLKDEV_GET_BY_PATH && !defined HAVE_BLKDEV_GET_BY_PATH_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,
                                        void *holder);
#endif

#ifdef HAVE_BD_SUPER
#define dattobd_get_super(bdev) (bdev)->bd_super
#define dattobd_drop_super(sb)
#elif defined HAVE_GET_SUPER
#define dattobd_get_super(bdev) get_super(bdev)
#define dattobd_drop_super(sb) drop_super(sb)
#else 
struct super_block* get_superblock(struct block_device*);
#define dattobd_get_super(bdev) get_superblock (bdev)
#define dattobd_drop_super(sb) drop_super(sb)
#endif

#endif /* BLKDEV_H_ */
