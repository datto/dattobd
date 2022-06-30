// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "callback_refs.h"

struct gendisk_tracking_data {
        const struct block_device *gtd_disk;
        BIO_REQUEST_TRACKING_PTR_TYPE *gtd_orig;
        atomic_t gtd_count;

        struct hlist_node node;
};

#define MAX_BUCKETS_BITS 2 // 2^2 == 4
DEFINE_HASHTABLE(gendisk_tracking_map, MAX_BUCKETS_BITS); 

void gendisk_tracking_init(void) {
        hash_init(gendisk_tracking_map);
}

static struct gendisk_tracking_data* get_a_node(const struct block_device *disk)
{
        unsigned int bkt = 0;
        struct gendisk_tracking_data *cur = NULL;
        hash_for_each(gendisk_tracking_map, bkt, cur, node) {
                if (cur->gtd_disk == disk) {
                        return cur;
                }
        }
        return NULL;
}

int gendisk_fn_get(const struct block_device *disk, const BIO_REQUEST_TRACKING_PTR_TYPE *fn) 
{
        struct gendisk_tracking_data* gtd = get_a_node(disk);
        if (!gtd) {
                gtd = kzalloc(sizeof(struct gendisk_tracking_data), GFP_KERNEL);
                if (!gtd) {
                        return -ENOMEM;
                }
                gtd->gtd_disk = disk;
                gtd->gtd_orig = (BIO_REQUEST_TRACKING_PTR_TYPE*) fn;
                // kzalloc guarantees gtd->gtd_count = { 0 };
                hash_add(gendisk_tracking_map, &gtd->node, (unsigned long)disk);
        }

        atomic_inc(&gtd->gtd_count);

        return 0;
}

const BIO_REQUEST_TRACKING_PTR_TYPE* gendisk_fn_put(struct block_device *disk)
{
        BIO_REQUEST_TRACKING_PTR_TYPE *fn = NULL;
        struct gendisk_tracking_data *gtd = get_a_node(disk);
        if (!gtd) {
                return NULL; // error here instead? essentially a double free
        }

        fn = gtd->gtd_orig;
        if (atomic_dec_and_test(&gtd->gtd_count)) { // if gtd_count IS zero
                hash_del(&gtd->node);
                kfree(gtd);
                return (const BIO_REQUEST_TRACKING_PTR_TYPE*) fn; // return the orig if this is our last ref
        }
        
        // return the current mrf to make swap logic easier
        return (const BIO_REQUEST_TRACKING_PTR_TYPE*) GET_BIO_REQUEST_TRACKING_PTR(disk);
}

size_t gendisk_fn_refcount(const BIO_REQUEST_TRACKING_PTR_TYPE *fn)
{
        unsigned int bkt = 0;
        struct gendisk_tracking_data *cur = NULL;
        size_t refcount = 0;
        hash_for_each(gendisk_tracking_map, bkt, cur, node) {
                if (cur->gtd_orig == fn) ++refcount;
        }
        return refcount;
}
