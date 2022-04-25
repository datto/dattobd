// SPDX-License-Identifier: GPL-2.0-only

/*
 * Contains code related to manipulating a bio_queue structure.
 *
 * Copyright (C) 2022 Datto Inc.
 */

#include "bio_queue.h"
#include "bio_helper.h"

/**
 * bio_queue_init() - Prepares a queue for use.
 * @bq: The queue.
 */
void bio_queue_init(struct bio_queue *bq)
{
        bio_list_init(&bq->bios);
        spin_lock_init(&bq->lock);
        init_waitqueue_head(&bq->event);
}

/**
 * bio_queue_empty() - Checks if the supplied queue is empty.
 * @bq: The queue.
 *
 * Return:
 * * 0  - not empty
 * * !0 - empty
 */
int bio_queue_empty(const struct bio_queue *bq)
{
        return bio_list_empty(&bq->bios);
}

/**
 * bio_queue_add() - Adds an element.
 * @bq: The queue.
 * @bio: The element to be added to the queue.
 *
 * Adds the supplied element @bio to the queue @bq.
 */
void bio_queue_add(struct bio_queue *bq, struct bio *bio)
{
        unsigned long flags;

        spin_lock_irqsave(&bq->lock, flags);
        bio_list_add(&bq->bios, bio);
        spin_unlock_irqrestore(&bq->lock, flags);
        wake_up(&bq->event);
}

/**
 * bio_queue_dequeue() - Retrieves an element.
 * @bq: The queue.
 *
 * This removes an element from the queue @bq and returns it to the caller.
 * Queued elements from @bq are removed in first-in-first-out order.
 *
 * Return: The removed element.
 */
struct bio *bio_queue_dequeue(struct bio_queue *bq)
{
        unsigned long flags;
        struct bio *bio;

        spin_lock_irqsave(&bq->lock, flags);
        bio = bio_list_pop(&bq->bios);
        spin_unlock_irqrestore(&bq->lock, flags);

        return bio;
}

/**
 * bio_overlap() - Checks for overlap between two block I/O operations.
 * @bio1: A first block I/O operation.
 * @bio2: A second block I/O operation.
 *
 * Return:
 * * 0  - no overlap between operations exists
 * * !0 - overlap exists
 */
static int bio_overlap(const struct bio *bio1, const struct bio *bio2)
{
        return max(bio_sector(bio1), bio_sector(bio2)) <=
               min(bio_sector(bio1) + (bio_size(bio1) / SECTOR_SIZE),
                   bio_sector(bio2) + (bio_size(bio2) / SECTOR_SIZE));
}

/**
 * bio_queue_dequeue_delay_read() - Dequeues the next &struct bio to be
 * processed with special consideration given for read operations that
 * have overlapping write operations yet to be processed.
 *
 * @bq: The &struct bio_queue object pointer.
 *
 * If the bio at the head of the @bq is a read operation and there is an
 * overlapping write operation pending then return the pending write
 * operation and delay the read operation by reinserting it at the tail of @bq.
 *
 * Context:
 * This call requires that the queue @bq be non-empty.
 *
 * Return: The block I/O operation scheduled for reading.
 */
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
