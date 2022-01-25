// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef BIO_QUEUE_H_
#define BIO_QUEUE_H_

#include "includes.h"

struct bio_queue {
        struct bio_list bios;
        spinlock_t lock;
        wait_queue_head_t event;
};

void bio_queue_init(struct bio_queue *bq);

int bio_queue_empty(const struct bio_queue *bq);

void bio_queue_add(struct bio_queue *bq, struct bio *bio);

struct bio *bio_queue_dequeue(struct bio_queue *bq);

int bio_overlap(const struct bio *bio1, const struct bio *bio2);

struct bio *bio_queue_dequeue_delay_read(struct bio_queue *bq);

#endif /* BIO_QUEUE_H_ */
