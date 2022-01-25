// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "bio_queue.h"
#include "bio_helper.h"

void bio_queue_init(struct bio_queue *bq)
{
        bio_list_init(&bq->bios);
        spin_lock_init(&bq->lock);
        init_waitqueue_head(&bq->event);
}

int bio_queue_empty(const struct bio_queue *bq)
{
        return bio_list_empty(&bq->bios);
}

void bio_queue_add(struct bio_queue *bq, struct bio *bio)
{
        unsigned long flags;

        spin_lock_irqsave(&bq->lock, flags);
        bio_list_add(&bq->bios, bio);
        spin_unlock_irqrestore(&bq->lock, flags);
        wake_up(&bq->event);
}

struct bio *bio_queue_dequeue(struct bio_queue *bq)
{
        unsigned long flags;
        struct bio *bio;

        spin_lock_irqsave(&bq->lock, flags);
        bio = bio_list_pop(&bq->bios);
        spin_unlock_irqrestore(&bq->lock, flags);

        return bio;
}

int bio_overlap(const struct bio *bio1, const struct bio *bio2)
{
        return max(bio_sector(bio1), bio_sector(bio2)) <=
               min(bio_sector(bio1) + (bio_size(bio1) / SECTOR_SIZE),
                   bio_sector(bio2) + (bio_size(bio2) / SECTOR_SIZE));
}

struct bio *bio_queue_dequeue_delay_read(struct bio_queue *bq)
{
        unsigned long flags;
        struct bio *bio, *tmp, *prev = NULL;

        spin_lock_irqsave(&bq->lock, flags);
        bio = bio_list_pop(&bq->bios);

        if (!bio_data_dir(bio)) {
                bio_list_for_each (tmp, &bq->bios) {
                        if (bio_data_dir(tmp) && bio_overlap(bio, tmp)) {
                                if (prev)
                                        prev->bi_next = bio;
                                else
                                        bq->bios.head = bio;

                                if (bq->bios.tail == tmp)
                                        bq->bios.tail = bio;

                                bio->bi_next = tmp->bi_next;
                                tmp->bi_next = NULL;
                                bio = tmp;

                                goto out;
                        }
                        prev = tmp;
                }
        }

out:
        spin_unlock_irqrestore(&bq->lock, flags);

        return bio;
}
