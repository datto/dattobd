/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#ifndef LIBELASTIO_SNAP_H_
#define LIBELASTIO_SNAP_H_

#include "elastio-snap.h"

#ifdef __cplusplus
extern "C" {
#endif

int elastio_snap_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size);

int elastio_snap_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int elastio_snap_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int elastio_snap_destroy(unsigned int minor);

int elastio_snap_transition_incremental(unsigned int minor);

int elastio_snap_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space);

int elastio_snap_reconfigure(unsigned int minor, unsigned long cache_size);

int elastio_snap_info(unsigned int minor, struct elastio_snap_info *info);

/**
 * Get the first available minor.
 *
 * @returns non-negative number if minor is available, otherwise -1
 */
int elastio_snap_get_free_minor(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBELASTIO_SNAP_H_ */
