// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"
#include "snap_device.h"

/**
 * tracer_read_fail_state() - Returns the error code currently set for the
 *                            &struct snap_device.
 * @dev: The &struct snap_device object pointer.
 *
 * Return: the error code, zero indicates no error set.
 */
int tracer_read_fail_state(const struct snap_device *dev)
{
        smp_mb();
        return atomic_read(&dev->sd_fail_code);
}

/**
 * tracer_set_fail_state() - Sets an error code for the &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 * @error: The error to set.
 */
void tracer_set_fail_state(struct snap_device *dev, int error)
{
        smp_mb();
        (void)atomic_cmpxchg(&dev->sd_fail_code, 0, error);
        smp_mb();
}
