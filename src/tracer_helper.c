// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"
#include "snap_device.h"

int tracer_read_fail_state(const struct snap_device *dev)
{
        smp_mb();
        return atomic_read(&dev->sd_fail_code);
}

void tracer_set_fail_state(struct snap_device *dev, int error)
{
        smp_mb();
        (void)atomic_cmpxchg(&dev->sd_fail_code, 0, error);
        smp_mb();
}
