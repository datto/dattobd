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


struct block_device *dattodb_blkdev_by_path(const char *path, fmode_t mode,
                                        void *holder);

struct super_block *dattobd_get_super(struct block_device * bd);

void dattobd_drop_super(struct super_block *sb);

#endif /* BLKDEV_H_ */
