/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Assurio Software, Inc are Copyright (C) 2019 Assurio Software Inc.
 */

#ifndef LIBASSURIO_SNAP_H_
#define LIBASSURIO_SNAP_H_

#include "assurio-snap.h"

#ifdef __cplusplus
extern "C" {
#endif

int assurio_snap_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size);

int assurio_snap_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int assurio_snap_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int assurio_snap_destroy(unsigned int minor);

int assurio_snap_transition_incremental(unsigned int minor);

int assurio_snap_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space);

int assurio_snap_reconfigure(unsigned int minor, unsigned long cache_size);

int assurio_snap_info(unsigned int minor, struct assurio_snap_info *info);

/**
 * Get the first available minor.
 *
 * @returns non-negative number if minor is available, otherwise -1
 */
int assurio_snap_get_free_minor(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBASSURIO_SNAP_H_ */
