// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef SSET_QUEUE_H_
#define SSET_QUEUE_H_

#include "includes.h"
#include "sset_list.h"

struct sset_queue {
        struct sset_list ssets;
        spinlock_t lock;
        wait_queue_head_t event;
};

void sset_queue_init(struct sset_queue *sq);

int sset_queue_empty(const struct sset_queue *sq);

void sset_queue_add(struct sset_queue *sq, struct sector_set *sset);

struct sector_set *sset_queue_dequeue(struct sset_queue *sq);

#endif /* SSET_QUEUE_H_ */
