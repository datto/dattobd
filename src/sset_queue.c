// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "sset_queue.h"
#include "sset_list.h"

/**
 * sset_queue_init() - Initialized a &struct sset_queue for use.
 * @sq: The &struct sset_queue object pointer.
 */
void sset_queue_init(struct sset_queue *sq)
{
        sset_list_init(&sq->ssets);
        spin_lock_init(&sq->lock);
        init_waitqueue_head(&sq->event);
}

/**
 * sset_queue_empty() - Checks to see if the supplied &struct sset_queue
 * is empty.
 *
 * @sq: The &struct sset_queue object pointer.
 *
 * Return:
 * * 0 - when empty
 * * !0 - otherwise
 */
int sset_queue_empty(const struct sset_queue *sq)
{
        return sset_list_empty(&sq->ssets);
}

/**
 * sset_queue_add() - adds @sset to the queue @sq.
 *
 * @sq: The &struct sset_queue object pointer.
 * @sset: The &struct sector_set object pointer t obe added to the @sq.
 */
void sset_queue_add(struct sset_queue *sq, struct sector_set *sset)
{
        unsigned long flags;

        spin_lock_irqsave(&sq->lock, flags);
        sset_list_add(&sq->ssets, sset);
        spin_unlock_irqrestore(&sq->lock, flags);
        wake_up(&sq->event);
}

/**
 * sset_queue_dequeue() - Dequeues an element from @sq.
 *
 * @sq: The &struct sset_queue object pointer.
 *
 * Return: The element at the head of the queue, NULL if empty.
 */
struct sector_set *sset_queue_dequeue(struct sset_queue *sq)
{
        unsigned long flags;
        struct sector_set *sset;

        spin_lock_irqsave(&sq->lock, flags);
        sset = sset_list_pop(&sq->ssets);
        spin_unlock_irqrestore(&sq->lock, flags);

        return sset;
}
