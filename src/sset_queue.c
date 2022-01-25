// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "sset_queue.h"
#include "sset_list.h"

void sset_queue_init(struct sset_queue *sq)
{
        sset_list_init(&sq->ssets);
        spin_lock_init(&sq->lock);
        init_waitqueue_head(&sq->event);
}

int sset_queue_empty(const struct sset_queue *sq)
{
        return sset_list_empty(&sq->ssets);
}

void sset_queue_add(struct sset_queue *sq, struct sector_set *sset)
{
        unsigned long flags;

        spin_lock_irqsave(&sq->lock, flags);
        sset_list_add(&sq->ssets, sset);
        spin_unlock_irqrestore(&sq->lock, flags);
        wake_up(&sq->event);
}

struct sector_set *sset_queue_dequeue(struct sset_queue *sq)
{
        unsigned long flags;
        struct sector_set *sset;

        spin_lock_irqsave(&sq->lock, flags);
        sset = sset_list_pop(&sq->ssets);
        spin_unlock_irqrestore(&sq->lock, flags);

        return sset;
}
