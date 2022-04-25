// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */
#ifndef SNAP_OPS_H_
#define SNAP_OPS_H_

const struct block_device_operations *get_snap_ops(void);

#endif /* SNAP_OPS_H_ */
