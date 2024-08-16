// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "tracer_helper.h"

#include "bio_helper.h"
#include "includes.h"
#include "snap_device.h"
#include "logging.h"
#include "blkdev.h"


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
        sector_t bio_sector_start = 0;
        if (!dev) {
                return false;
        }
        
        bio_sector_start = bio_sector(bio);

        smp_mb();
        active = atomic_read(&dev->sd_active);

        if(unlikely(test_bit(UNVERIFIED, &dev->sd_state)) || unlikely(!active))
                return false;

        if(unlikely(!tracer_queue_matches_bio(dev, bio)))
                return false;

#if defined HAVE_BIO_BI_BDEV
        if(unlikely(bio->bi_bdev == NULL))
                return false;
        bio_sector_start += get_start_sect(bio->bi_bdev);
#elif defined HAVE_BIO_BI_PARTNO
        if(unlikely(bio->bi_disk == NULL))
                return false;
        sector_t offset;
        if(dattobd_get_start_sect_by_gendisk_for_bio(bio->bi_disk, bio->bi_partno, &offset)){
                return false;
        }
        bio_sector_start += offset;
#else
        #error struct bio has neither bi_bdev nor bi_partno.
#endif

        if(likely(bio_sector_start >= dev->sd_sect_off && bio_sector_start + bio_sectors(bio) <= dev->sd_sect_off + dev->sd_size))
                return true;
        
        if(likely(bio_sector_start >= dev->sd_sect_off + dev->sd_size || bio_sector_start + bio_sectors(bio) <= dev->sd_sect_off))
                return false;
        
        LOG_WARN("bio and snap_device have intersecting sector range! this may cause the corruption! bio: start=%llu size=%u; snap_device: start=%llu size=%llu", bio_sector_start, bio_sectors(bio), dev->sd_sect_off, dev->sd_size);
        return false;
}

bool tracer_is_bio_for_dev_only_queue(struct snap_device *dev, struct bio *bio)
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
