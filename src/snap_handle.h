// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef SNAP_HANDLE_H_
#define SNAP_HANDLE_H_

struct snap_device;
struct bio;
struct sector_set;

int snap_handle_read_bio(const struct snap_device *dev, struct bio *bio);

int snap_handle_write_bio(const struct snap_device *dev, struct bio *bio);

int inc_handle_sset(const struct snap_device *dev, struct sector_set *sset);

#endif /* SNAP_HANDLE_H_ */
