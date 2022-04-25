// SPDX-License-Identifier: GPL-2.0-only

/*
 * Contains code related to manipulating a bio_list structure.
 *
 * Copyright (C) 2022 Datto Inc.
 */

#include "bio_list.h"

#ifndef HAVE_BIO_LIST
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)

/**
 * bio_list_empty() - Checks if the supplied list is empty.
 * @bl: The list.
 *
 * Return:
 * * 0  - not empty
 * * !0 - empty
 */
int bio_list_empty(const struct bio_list *bl)
{
        return bl->head == NULL;
}

/**
 * bio_list_init() - Prepares a list for use.
 * @bl: The list.
 */
void bio_list_init(struct bio_list *bl)
{
        bl->head = bl->tail = NULL;
}

/**
 * bio_list_add() - Adds an element.
 * @bl: The list.
 * @bio: The element to be added to the list.
 *
 * Adds the supplied element @bio to the end of the list @bl.
 */
void bio_list_add(struct bio_list *bl, struct bio *bio)
{
        bio->bi_next = NULL;

        if (bl->tail)
                bl->tail->bi_next = bio;
        else
                bl->head = bio;

        bl->tail = bio;
}

/**
 * bio_list_pop() - Retrieves an element.
 * @bl: The list.
 *
 * This removes an element from the list @bl and returns it to the caller.
 * Elements from @bl are always removed in first-in-first-out order.
 *
 * Return: The removed element.
 */
struct bio *bio_list_pop(struct bio_list *bl)
{
        struct bio *bio = bl->head;

        if (bio) {
                bl->head = bl->head->bi_next;
                if (!bl->head)
                        bl->tail = NULL;

                bio->bi_next = NULL;
        }

        return bio;
}

#endif
