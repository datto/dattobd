// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "tracer_helper.h"

#include "bio_helper.h"
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

bool tracer_is_bio_for_dev(struct snap_device *dev, struct bio *bio)
{
        int active = 0;
        if (!dev) {
                return false;
        }

        smp_mb();
        active = atomic_read(&dev->sd_active);

        return !test_bit(UNVERIFIED, &dev->sd_state)
                && tracer_queue_matches_bio(dev, bio)
                && active;
}

bool tracer_should_trace_bio(struct snap_device *dev, struct bio *bio)
{
        return dev 
                && !bio_is_discard(bio)
                && bio_data_dir(bio)
                && bio_size(bio) 
                && !tracer_read_fail_state(dev)
                && tracer_sector_matches_bio(dev, bio); 
}
