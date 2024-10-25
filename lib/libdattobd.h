/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * Copyright (C) 2015 Datto Inc.
 */

#ifndef LIBDATTOBD_H_
#define LIBDATTOBD_H_

#include "dattobd.h"

#ifdef __cplusplus
extern "C" {
#endif

int dattobd_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size);

int dattobd_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int dattobd_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int dattobd_destroy(unsigned int minor);

int dattobd_transition_incremental(unsigned int minor);

int dattobd_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space);

int dattobd_reconfigure(unsigned int minor, unsigned long cache_size);

int dattobd_info(unsigned int minor, struct dattobd_info *info);

int dattobd_expand_cow_file(unsigned int minor, unsigned long size);

/**
 * Get the first available minor.
 *
 * @returns non-negative number if minor is available, otherwise -1
 */
int dattobd_get_free_minor(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBDATTOBD_H_ */
