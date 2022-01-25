// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "bio_list.h"

#ifndef HAVE_BIO_LIST
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)

int bio_list_empty(const struct bio_list *bl)
{
        return bl->head == NULL;
}

void bio_list_init(struct bio_list *bl)
{
        bl->head = bl->tail = NULL;
}

void bio_list_add(struct bio_list *bl, struct bio *bio)
{
        bio->bi_next = NULL;

        if (bl->tail)
                bl->tail->bi_next = bio;
        else
                bl->head = bio;

        bl->tail = bio;
}

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
