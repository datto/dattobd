// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef COW_MANAGER_H_
#define COW_MANAGER_H_

#include "dattobd.h"
#include "filesystem.h"

#ifndef __KERNEL__
#include <stdint.h>
#endif

#define COW_SECTION_SIZE 4096

#define cow_write_filler_mapping(cm, pos) __cow_write_mapping(cm, pos, 1)
static unsigned long dattobd_cow_ext_buf_size = sizeof(struct fiemap_extent) * 1024;

/**
 * struct cow_section - maintains data and usage statistics for a cow section.
 *
 * A &struct cow_section manages the basic unit of data the COW manager works
 * with and represents a section which is 4K sectors.
 */
struct cow_section {
        /**
         * @has_data: zero if this section has mappings (on file or in memory)
         */
        char has_data;

        /**
         * @usage: counter that keeps track of how often this section is used
         */
        unsigned long usage;

        /** @mappings: array of block addresses */
        uint64_t *mappings;
};

struct cow_manager {
        struct file *filp; // the file the cow manager is writing to
        uint32_t flags; // flags representing current state of cow manager
        uint64_t curr_pos; // current write head position
        uint64_t data_offset; // starting offset of data
        uint64_t file_max; // max size of the file before an error is thrown
        uint64_t seqid; // sequence id, increments on each transition to
                        // snapshot mode
        uint64_t version; // version of cow file format
        uint64_t nr_changed_blocks; // number of changed blocks since last
                                    // snapshot
        uint8_t uuid[COW_UUID_SIZE]; // uuid for this series of snaphots
        unsigned int log_sect_pages; // log2 of the number of pages needed to
                                     // store a section
        unsigned long sect_size; // size of a section in number of elements it
                                 // can contain
        unsigned long allocated_sects; // number of currently allocated sections
        unsigned long total_sects; // total sections the cm log represents
        unsigned long allowed_sects; // the maximum number of sections that may
                                     // be allocated at once
        struct cow_section *sects; // pointer to the array of sections of
                                   // mappings
        struct snap_device* dev;  //pointer to snapshot device
};

/***************************COW MANAGER FUNCTIONS**************************/

void cow_free_members(struct cow_manager *cm);

void cow_free(struct cow_manager *cm);

int cow_sync_and_free(struct cow_manager *cm);

int cow_sync_and_close(struct cow_manager *cm);

int cow_reopen(struct cow_manager *cm, const char *pathname);

int cow_reload(const char *path, uint64_t elements, unsigned long sect_size,
               unsigned long cache_size, int index_only,
               struct cow_manager **cm_out);

int cow_init(const char *path, uint64_t elements, unsigned long sect_size,
             unsigned long cache_size, uint64_t file_max, const uint8_t *uuid,
             uint64_t seqid, struct cow_manager **cm_out);

int cow_truncate_to_index(struct cow_manager *cm);

void cow_modify_cache_size(struct cow_manager *cm, unsigned long cache_size);

int cow_read_mapping(struct cow_manager *cm, uint64_t pos, uint64_t *out);

int cow_write_current(struct cow_manager *cm, uint64_t block, void *buf);

int cow_read_data(struct cow_manager *cm, void *buf, uint64_t block_pos,
                  unsigned long block_off, unsigned long len);

int __cow_write_mapping(struct cow_manager *cm, uint64_t pos, uint64_t val);

int cow_get_file_extents(struct snap_device* dev, struct file* filp);

#endif /* COW_MANAGER_H_ */
