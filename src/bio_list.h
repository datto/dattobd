// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef BIO_LIST_H_
#define BIO_LIST_H_

#ifndef HAVE_BIO_LIST
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)

struct bio;

struct bio_list {
        struct bio *head;
        struct bio *tail;
};

#define BIO_EMPTY_LIST                                                         \
        {                                                                      \
                NULL, NULL                                                     \
        }
#define bio_list_for_each(bio, bl)                                             \
        for ((bio) = (bl)->head; (bio); (bio) = (bio)->bi_next)

int bio_list_empty(const struct bio_list *bl);
void bio_list_init(struct bio_list *bl);
void bio_list_add(struct bio_list *bl, struct bio *bio);
struct bio *bio_list_pop(struct bio_list *bl);

#endif

#endif /* BIO_LIST_H_ */
