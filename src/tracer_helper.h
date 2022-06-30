// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef TRACER_HELPER_H_
#define TRACER_HELPER_H_

#include "bio_helper.h"
#include "hints.h"
#include "includes.h"
#include "module_control.h"

// macro for iterating over snap_devices (requires a null check on dev)
#define tracer_for_each(dev, i)                                                \
        for (i = ACCESS_ONCE(lowest_minor),                                    \
            dev = ACCESS_ONCE(snap_devices[i]);                                \
             i <= ACCESS_ONCE(highest_minor);                                  \
             i++, dev = ACCESS_ONCE(snap_devices[i]))
#define tracer_for_each_full(dev, i)                                           \
        for (i = 0, dev = ACCESS_ONCE(snap_devices[i]);                        \
             i < dattobd_max_snap_devices;                                     \
             i++, dev = ACCESS_ONCE(snap_devices[i]))

// returns true if tracing struct's base device queue matches that of bio
#define tracer_queue_matches_bio(dev, bio)                                     \
        (bdev_get_queue((dev)->sd_base_dev) == dattobd_bio_get_queue(bio))

// returns true if tracing struct's sector range matches the sector of the bio
#define tracer_sector_matches_bio(dev, bio)                                    \
        (bio_sector(bio) >= (dev)->sd_sect_off &&                              \
         bio_sector(bio) < (dev)->sd_sect_off + (dev)->sd_size)

/**
 * tracer_is_bio_for_dev() - Check if bio is intended for given snap_device.
 *
 * returns true if bio's queue matches the devices' queue and partition, and if
 * bio has a size, tracing struct is in non-fail state, and the
 * device's sector range matches the bio.
 *
 * @dev: The snap_device to check the bio against.
 * @bio: The bio to check the snap device against.
 *
 * Return: True if bio is for dev (+ conditions described above). 
 *         False otherwise.
 */
bool tracer_is_bio_for_dev(struct snap_device *dev, struct bio *bio);

/**
 * tracer_should_trace_bio() - Check if given bio should be traced.
 *
 * Return: Returns true if dev is not null
 *         and if bio has a size, is a write, and it's tracing struct is in a
 *         non-fail state, and the device's sector range matches the bio.
 */
bool tracer_should_trace_bio(struct snap_device *dev, struct bio *bio);

struct snap_device;

/**
 * tracer_read_fail_state() - Returns the error code currently set for the
 *                            &struct snap_device.
 * @dev: The &struct snap_device object pointer.
 *
 * Return: the error code, zero indicates no error set.
 */
int tracer_read_fail_state(const struct snap_device *dev);

/**
 * tracer_set_fail_state() - Sets an error code for the &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 * @error: The error to set.
 */
void tracer_set_fail_state(struct snap_device *dev, int error);

#endif /* TRACER_HELPER_H_ */
