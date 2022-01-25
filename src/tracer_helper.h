// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef TRACER_HELPER_H_
#define TRACER_HELPER_H_

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

// should be called along with *_queue_matches_bio to be valid. returns true if
// bio is a write, has a size, tracing struct is in non-fail state, and the
// device's sector range matches the bio
#define tracer_should_trace_bio(dev, bio)                                      \
        (bio_data_dir(bio) && !bio_is_discard(bio) && bio_size(bio) &&         \
         !tracer_read_fail_state(dev) && tracer_sector_matches_bio(dev, bio))

struct snap_device;

int tracer_read_fail_state(const struct snap_device *dev);
void tracer_set_fail_state(struct snap_device *dev, int error);

#endif /* TRACER_HELPER_H_ */
