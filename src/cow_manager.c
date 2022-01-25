// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "cow_manager.h"
#include "filesystem.h"
#include "logging.h"

#ifdef HAVE_UUID_H
#include <linux/uuid.h>
#endif

#ifndef HAVE_VZALLOC
#define vzalloc(size)                                                          \
        __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL)
#endif

#define __cow_write_header_dirty(cm) __cow_write_header(cm, 0)
#define __cow_close_header(cm) __cow_write_header(cm, 1)
#define __cow_write_current_mapping(cm, pos)                                   \
        __cow_write_mapping(cm, pos, (cm)->curr_pos)

// memory macros
#define get_zeroed_pages(flags, order)                                         \
        __get_free_pages(((flags) | __GFP_ZERO), order)

void __cow_free_section(struct cow_manager *cm, unsigned long sect_idx)
{
        free_pages((unsigned long)cm->sects[sect_idx].mappings,
                   cm->log_sect_pages);
        cm->sects[sect_idx].mappings = NULL;
        cm->allocated_sects--;
}

int __cow_alloc_section(struct cow_manager *cm, unsigned long sect_idx,
                        int zero)
{
        if (zero)
                cm->sects[sect_idx].mappings = (void *)get_zeroed_pages(
                        GFP_KERNEL, cm->log_sect_pages);
        else
                cm->sects[sect_idx].mappings = (void *)__get_free_pages(
                        GFP_KERNEL, cm->log_sect_pages);

        if (!cm->sects[sect_idx].mappings) {
                LOG_ERROR(-ENOMEM, "failed to allocate mappings at index %lu",
                          sect_idx);
                return -ENOMEM;
        }

        cm->sects[sect_idx].has_data = 1;
        cm->allocated_sects++;

        return 0;
}

int __cow_load_section(struct cow_manager *cm, unsigned long sect_idx)
{
        int ret;

        ret = __cow_alloc_section(cm, sect_idx, 0);
        if (ret)
                goto error;

        ret = file_read(cm->filp, cm->sects[sect_idx].mappings,
                        cm->sect_size * sect_idx * 8 + COW_HEADER_SIZE,
                        cm->sect_size * 8);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error loading section from file");
        if (cm->sects[sect_idx].mappings)
                __cow_free_section(cm, sect_idx);
        return ret;
}

int __cow_write_section(struct cow_manager *cm, unsigned long sect_idx)
{
        int ret;

        ret = file_write(cm->filp, cm->sects[sect_idx].mappings,
                         cm->sect_size * sect_idx * 8 + COW_HEADER_SIZE,
                         cm->sect_size * 8);
        if (ret) {
                LOG_ERROR(ret, "error writing cow manager section to file");
                return ret;
        }

        return 0;
}

int __cow_sync_and_free_sections(struct cow_manager *cm, unsigned long thresh)
{
        int ret;
        unsigned long i;

        for (i = 0; i < cm->total_sects &&
                    (!thresh || cm->allocated_sects > cm->allowed_sects / 2);
             i++) {
                if (cm->sects[i].mappings &&
                    (!thresh || cm->sects[i].usage <= thresh)) {
                        ret = __cow_write_section(cm, i);
                        if (ret) {
                                LOG_ERROR(ret,
                                          "error writing cow manager section "
                                          "%lu to file",
                                          i);
                                return ret;
                        }

                        __cow_free_section(cm, i);
                }
                cm->sects[i].usage = 0;
        }

        return 0;
}

/***************************COW MANAGER FUNCTIONS**************************/

int __cow_cleanup_mappings(struct cow_manager *cm)
{
        unsigned long i;
        int ret;
        unsigned long granularity, thresh = 0;

        // find the max usage of the sections of the cm
        for (i = 0; i < cm->total_sects; i++) {
                if (cm->sects[i].usage > thresh)
                        thresh = cm->sects[i].usage;
        }

        // find the (approximate) median usage of the sections of the cm
        thresh /= 2;
        granularity = thresh;
        while (granularity > 0) {
                unsigned long less, greater;
                granularity = granularity >> 1;
                less = 0;
                greater = 0;
                for (i = 0; i < cm->total_sects; i++) {
                        if (cm->sects[i].usage <= thresh)
                                less++;
                        else
                                greater++;
                }

                if (greater > less)
                        thresh += granularity;
                else if (greater < less)
                        thresh -= granularity;
                else
                        break;
        }

        // deallocate sections of the cm with less usage than the median
        ret = __cow_sync_and_free_sections(cm, thresh);
        if (ret) {
                LOG_ERROR(ret, "error cleaning cow manager mappings");
                return ret;
        }

        return 0;
}

int __cow_write_header(struct cow_manager *cm, int is_clean)
{
        int ret;
        struct cow_header ch;

        if (is_clean)
                cm->flags |= (1 << COW_CLEAN);
        else
                cm->flags &= ~(1 << COW_CLEAN);

        ch.magic = COW_MAGIC;
        ch.flags = cm->flags;
        ch.fpos = cm->curr_pos;
        ch.fsize = cm->file_max;
        ch.seqid = cm->seqid;
        memcpy(ch.uuid, cm->uuid, COW_UUID_SIZE);
        ch.version = cm->version;
        ch.nr_changed_blocks = cm->nr_changed_blocks;

        ret = file_write(cm->filp, &ch, 0, sizeof(struct cow_header));
        if (ret) {
                LOG_ERROR(ret, "error syncing cow manager header");
                return ret;
        }

        return 0;
}

int __cow_open_header(struct cow_manager *cm, int index_only, int reset_vmalloc)
{
        int ret;
        struct cow_header ch;

        ret = file_read(cm->filp, &ch, 0, sizeof(struct cow_header));
        if (ret)
                goto error;

        if (ch.magic != COW_MAGIC) {
                ret = -EINVAL;
                LOG_ERROR(-EINVAL, "bad magic number found in cow file: %lu",
                          ((unsigned long)ch.magic));
                goto error;
        }

        if (!(ch.flags & (1 << COW_CLEAN))) {
                ret = -EINVAL;
                LOG_ERROR(-EINVAL, "cow file not left in clean state: %lu",
                          ((unsigned long)ch.flags));
                goto error;
        }

        if (((ch.flags & (1 << COW_INDEX_ONLY)) && !index_only) ||
            (!(ch.flags & (1 << COW_INDEX_ONLY)) && index_only)) {
                ret = -EINVAL;
                LOG_ERROR(-EINVAL, "cow file not left in %s state: %lu",
                          ((index_only) ? "index only" : "data tracking"),
                          (unsigned long)ch.flags);
                goto error;
        }

        LOG_DEBUG("cow header opened with file pos = %llu, seqid = %llu",
                  ((unsigned long long)ch.fpos), (unsigned long long)ch.seqid);

        if (reset_vmalloc)
                cm->flags = ch.flags & ~(1 << COW_VMALLOC_UPPER);
        else
                cm->flags = ch.flags;

        cm->curr_pos = ch.fpos;
        cm->file_max = ch.fsize;
        cm->seqid = ch.seqid;
        memcpy(cm->uuid, ch.uuid, COW_UUID_SIZE);
        cm->version = ch.version;
        cm->nr_changed_blocks = ch.nr_changed_blocks;

        ret = __cow_write_header_dirty(cm);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error opening cow manager header");
        return ret;
}

void cow_free_members(struct cow_manager *cm)
{
        if (cm->sects) {
                unsigned long i;
                for (i = 0; i < cm->total_sects; i++) {
                        if (cm->sects[i].mappings)
                                free_pages((unsigned long)cm->sects[i].mappings,
                                           cm->log_sect_pages);
                }

                if (cm->flags & (1 << COW_VMALLOC_UPPER))
                        vfree(cm->sects);
                else
                        kfree(cm->sects);

                cm->sects = NULL;
        }

        if (cm->filp) {
                file_unlink_and_close_force(cm->filp);
                cm->filp = NULL;
        }
}

void cow_free(struct cow_manager *cm)
{
        cow_free_members(cm);
        kfree(cm);
}

int cow_sync_and_free(struct cow_manager *cm)
{
        int ret;

        ret = __cow_sync_and_free_sections(cm, 0);
        if (ret)
                goto error;

        ret = __cow_close_header(cm);
        if (ret)
                goto error;

        if (cm->filp)
                file_close(cm->filp);

        if (cm->sects) {
                if (cm->flags & (1 << COW_VMALLOC_UPPER))
                        vfree(cm->sects);
                else
                        kfree(cm->sects);
        }

        kfree(cm);

        return 0;

error:
        LOG_ERROR(ret, "error while syncing and freeing cow manager");
        cow_free(cm);
        return ret;
}

int cow_sync_and_close(struct cow_manager *cm)
{
        int ret;

        ret = __cow_sync_and_free_sections(cm, 0);
        if (ret)
                goto error;

        ret = __cow_close_header(cm);
        if (ret)
                goto error;

        if (cm->filp)
                file_close(cm->filp);
        cm->filp = NULL;

        return 0;

error:
        LOG_ERROR(ret, "error while syncing and closing cow manager");
        cow_free_members(cm);
        return ret;
}

int cow_reopen(struct cow_manager *cm, const char *pathname)
{
        int ret;

        LOG_DEBUG("reopening cow file");
        ret = file_open(pathname, 0, &cm->filp);
        if (ret)
                goto error;

        LOG_DEBUG("opening cow header");
        ret = __cow_open_header(cm, (cm->flags & (1 << COW_INDEX_ONLY)), 0);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error reopening cow manager");
        if (cm->filp)
                file_close(cm->filp);
        cm->filp = NULL;

        return ret;
}

unsigned long __cow_calculate_allowed_sects(unsigned long cache_size,
                                            unsigned long total_sects)
{
        if (cache_size <= (total_sects * sizeof(struct cow_section)))
                return 0;
        else
                return (cache_size -
                        (total_sects * sizeof(struct cow_section))) /
                       (COW_SECTION_SIZE * 8);
}

int cow_reload(const char *path, uint64_t elements, unsigned long sect_size,
               unsigned long cache_size, int index_only,
               struct cow_manager **cm_out)
{
        int ret;
        unsigned long i;
        struct cow_manager *cm;

        LOG_DEBUG("allocating cow manager");
        cm = kzalloc(sizeof(struct cow_manager), GFP_KERNEL);
        if (!cm) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating cow manager");
                goto error;
        }

        LOG_DEBUG("opening cow file");
        ret = file_open(path, 0, &cm->filp);
        if (ret)
                goto error;

        cm->allocated_sects = 0;
        cm->sect_size = sect_size;
        cm->log_sect_pages = get_order(sect_size * 8);
        cm->total_sects =
                NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
        cm->allowed_sects =
                __cow_calculate_allowed_sects(cache_size, cm->total_sects);
        cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size * 8));

        ret = __cow_open_header(cm, index_only, 1);
        if (ret)
                goto error;

        LOG_DEBUG("allocating cow manager array (%lu sections)",
                  cm->total_sects);
        cm->sects = kzalloc((cm->total_sects) * sizeof(struct cow_section),
                            GFP_KERNEL | __GFP_NOWARN);
        if (!cm->sects) {
                // try falling back to vmalloc
                cm->flags |= (1 << COW_VMALLOC_UPPER);
                cm->sects =
                        vzalloc((cm->total_sects) * sizeof(struct cow_section));
                if (!cm->sects) {
                        ret = -ENOMEM;
                        LOG_ERROR(ret,
                                  "error allocating cow manager sects array");
                        goto error;
                }
        }

        for (i = 0; i < cm->total_sects; i++) {
                cm->sects[i].has_data = 1;
        }

        *cm_out = cm;
        return 0;

error:
        LOG_ERROR(ret, "error during cow manager initialization");
        if (cm->filp)
                file_close(cm->filp);

        if (cm->sects) {
                if (cm->flags & (1 << COW_VMALLOC_UPPER))
                        vfree(cm->sects);
                else
                        kfree(cm->sects);
        }

        if (cm)
                kfree(cm);

        *cm_out = NULL;
        return ret;
}

int cow_init(const char *path, uint64_t elements, unsigned long sect_size,
             unsigned long cache_size, uint64_t file_max, const uint8_t *uuid,
             uint64_t seqid, struct cow_manager **cm_out)
{
        int ret;
        struct cow_manager *cm;

        LOG_DEBUG("allocating cow manager, seqid = %llu",
                  (unsigned long long)seqid);
        cm = kzalloc(sizeof(struct cow_manager), GFP_KERNEL);
        if (!cm) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating cow manager");
                goto error;
        }

        LOG_DEBUG("creating cow file");
        ret = file_open(path, O_CREAT | O_TRUNC, &cm->filp);
        if (ret)
                goto error;

        cm->version = COW_VERSION_CHANGED_BLOCKS;
        cm->nr_changed_blocks = 0;
        cm->flags = 0;
        cm->allocated_sects = 0;
        cm->file_max = file_max;
        cm->sect_size = sect_size;
        cm->seqid = seqid;
        cm->log_sect_pages = get_order(sect_size * 8);
        cm->total_sects =
                NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
        cm->allowed_sects =
                __cow_calculate_allowed_sects(cache_size, cm->total_sects);
        cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size * 8));
        cm->curr_pos = cm->data_offset / COW_BLOCK_SIZE;

        if (uuid)
                memcpy(cm->uuid, uuid, COW_UUID_SIZE);
        else
                generate_random_uuid(cm->uuid);

        LOG_DEBUG("allocating cow manager array (%lu sections)",
                  cm->total_sects);
        cm->sects = kzalloc((cm->total_sects) * sizeof(struct cow_section),
                            GFP_KERNEL | __GFP_NOWARN);
        if (!cm->sects) {
                // try falling back to vmalloc
                cm->flags |= (1 << COW_VMALLOC_UPPER);
                cm->sects =
                        vzalloc((cm->total_sects) * sizeof(struct cow_section));
                if (!cm->sects) {
                        ret = -ENOMEM;
                        LOG_ERROR(ret,
                                  "error allocating cow manager sects array");
                        goto error;
                }
        }

        LOG_DEBUG("allocating cow file (%llu bytes)",
                  (unsigned long long)file_max);
        ret = file_allocate(cm->filp, 0, file_max);
        if (ret)
                goto error;

        ret = __cow_write_header_dirty(cm);
        if (ret)
                goto error;

        *cm_out = cm;
        return 0;

error:
        LOG_ERROR(ret, "error during cow manager initialization");
        if (cm->filp)
                file_unlink_and_close(cm->filp);

        if (cm->sects) {
                if (cm->flags & (1 << COW_VMALLOC_UPPER))
                        vfree(cm->sects);
                else
                        kfree(cm->sects);
        }

        if (cm)
                kfree(cm);

        *cm_out = NULL;
        return ret;
}

int cow_truncate_to_index(struct cow_manager *cm)
{
        // truncate the cow file to just the index
        cm->flags |= (1 << COW_INDEX_ONLY);
        return file_truncate(cm->filp, cm->data_offset);
}

void cow_modify_cache_size(struct cow_manager *cm, unsigned long cache_size)
{
        cm->allowed_sects =
                __cow_calculate_allowed_sects(cache_size, cm->total_sects);
}

int cow_read_mapping(struct cow_manager *cm, uint64_t pos, uint64_t *out)
{
        int ret;
        uint64_t sect_idx = pos;
        unsigned long sect_pos = do_div(sect_idx, cm->sect_size);

        cm->sects[sect_idx].usage++;

        if (!cm->sects[sect_idx].mappings) {
                if (!cm->sects[sect_idx].has_data) {
                        *out = 0;
                        return 0;
                } else {
                        ret = __cow_load_section(cm, sect_idx);
                        if (ret)
                                goto error;
                }
        }

        *out = cm->sects[sect_idx].mappings[sect_pos];

        if (cm->allocated_sects > cm->allowed_sects) {
                ret = __cow_cleanup_mappings(cm);
                if (ret)
                        goto error;
        }

        return 0;

error:
        LOG_ERROR(ret, "error reading cow mapping");
        return ret;
}

int __cow_write_mapping(struct cow_manager *cm, uint64_t pos, uint64_t val)
{
        int ret;
        uint64_t sect_idx = pos;
        unsigned long sect_pos = do_div(sect_idx, cm->sect_size);

        cm->sects[sect_idx].usage++;

        if (!cm->sects[sect_idx].mappings) {
                if (!cm->sects[sect_idx].has_data) {
                        ret = __cow_alloc_section(cm, sect_idx, 1);
                        if (ret)
                                goto error;
                } else {
                        ret = __cow_load_section(cm, sect_idx);
                        if (ret)
                                goto error;
                }
        }

        if (cm->version >= COW_VERSION_CHANGED_BLOCKS &&
            !cm->sects[sect_idx].mappings[sect_pos])
                cm->nr_changed_blocks++;

        cm->sects[sect_idx].mappings[sect_pos] = val;

        if (cm->allocated_sects > cm->allowed_sects) {
                ret = __cow_cleanup_mappings(cm);
                if (ret)
                        goto error;
        }

        return 0;

error:
        LOG_ERROR(ret, "error writing cow mapping");
        return ret;
}

int __cow_write_data(struct cow_manager *cm, void *buf)
{
        int ret;
        char *abs_path = NULL;
        int abs_path_len;
        uint64_t curr_size = cm->curr_pos * COW_BLOCK_SIZE;

        if (curr_size >= cm->file_max) {
                ret = -EFBIG;

                file_get_absolute_pathname(cm->filp, &abs_path, &abs_path_len);
                if (!abs_path) {
                        LOG_ERROR(ret, "cow file max size exceeded (%llu/%llu)",
                                  curr_size, cm->file_max);
                } else {
                        LOG_ERROR(ret,
                                  "cow file '%s' max size exceeded (%llu/%llu)",
                                  abs_path, curr_size, cm->file_max);
                        kfree(abs_path);
                }

                goto error;
        }

        ret = file_write(cm->filp, buf, curr_size, COW_BLOCK_SIZE);
        if (ret)
                goto error;

        cm->curr_pos++;

        return 0;

error:
        LOG_ERROR(ret, "error writing cow data");
        return ret;
}

int cow_write_current(struct cow_manager *cm, uint64_t block, void *buf)
{
        int ret;
        uint64_t block_mapping;

        // read this mapping from the cow manager
        ret = cow_read_mapping(cm, block, &block_mapping);
        if (ret)
                goto error;

        // if the block mapping already exists return so we don't overwrite it
        if (block_mapping)
                return 0;

        // write the mapping
        ret = __cow_write_current_mapping(cm, block);
        if (ret)
                goto error;

        // write the data
        ret = __cow_write_data(cm, buf);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error writing cow data and mapping");
        return ret;
}

int cow_read_data(struct cow_manager *cm, void *buf, uint64_t block_pos,
                  unsigned long block_off, unsigned long len)
{
        int ret;

        if (block_off >= COW_BLOCK_SIZE)
                return -EINVAL;

        ret = file_read(cm->filp, buf, (block_pos * COW_BLOCK_SIZE) + block_off,
                        len);
        if (ret) {
                LOG_ERROR(ret, "error reading cow data");
                return ret;
        }

        return 0;
}
