// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "cow_manager.h"
#include "filesystem.h"
#include "logging.h"
#include "tracer.h"
#include "blkdev.h"

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

inline void __close_and_destroy_dattobd_mutable_file(struct dattobd_mutable_file *dfilp){
        file_close(dfilp);
        dattobd_mutable_file_unwrap(dfilp);
}

inline int __open_dattobd_mutable_file(const char *path, int flags, struct dattobd_mutable_file **dfilp){
        struct file* filp = NULL;
        int ret;

        ret = file_open(path, flags, &filp);

        if(ret){
                LOG_ERROR(ret, "failed to open file");
                return ret;
        }

        *dfilp = dattobd_mutable_file_wrap(filp);

        if(IS_ERR(*dfilp)){
                LOG_ERROR(-ENOMEM, "failed to wrap file pointer");
                __file_close_raw(filp);
                // filp does not need to be kfreed here, it is handled by the filp_close under the hood
                return -ENOMEM;
        }

        return 0;
}

/**
 * __cow_free_section() - Frees the memory used to track the section at
 * offset @sect_idx and marks the array entry as unused.
 *
 * @cm: The &struct cow_manager tracking the block device.
 * @sect_idx: An offset into the array of sections used to track COW data.
 */
void __cow_free_section(struct cow_manager *cm, unsigned long sect_idx)
{
        free_pages((unsigned long)cm->sects[sect_idx].mappings,
                   cm->log_sect_pages);
        cm->sects[sect_idx].mappings = NULL;
        cm->allocated_sects--;
}

/**
 * __cow_alloc_section() - Allocates a section in the cache at offset
 * @sect_idx, marks it as having data and updates cache stats.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 * @sect_idx: the cow section index
 * @zero: an int encoded boolean value indicating whether to allocate mappings
 *        initially zeroed or with potentially random data.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
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

/**
 * __cow_load_section() - Allocates and reads a section from the COW backing
 * file
 * @cm: each &struct snap_device has a &struct cow_manager
 * @sect_idx: An offset into the array of sections used to track COW data.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __cow_load_section(struct cow_manager *cm, unsigned long sect_idx)
{
        int ret, i;
        int sect_size_bytes = COW_SECTION_SIZE * sizeof(uint64_t);

        ret = __cow_alloc_section(cm, sect_idx, 0);
        if (ret)
                goto error;

        for (i = 0; i < sect_size_bytes / COW_BLOCK_SIZE; i++) {
		int mapping_offset = (COW_BLOCK_SIZE / sizeof(cm->sects[sect_idx].mappings[0])) * i;
		int cow_file_offset = COW_BLOCK_SIZE * i;

                ret = file_read(cm->dfilp, cm->dev, cm->sects[sect_idx].mappings,
                                cm->sect_size * sect_idx * 8 + COW_HEADER_SIZE,
                                cm->sect_size * 8);
                if (ret)
                        goto error;
        }

        return 0;

error:
        LOG_ERROR(ret, "error loading section from file");
        if (cm->sects[sect_idx].mappings)
                __cow_free_section(cm, sect_idx);
        return ret;
}

/**
 * __cow_write_section() - Transfers the cached section to the backing file.
 * @cm: each &struct snap_device has a &struct cow_manager
 * @sect_idx: An offset into the array of sections used to track COW data.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __cow_write_section(struct cow_manager *cm, unsigned long sect_idx)
{
        int i, ret;
        int sect_size_bytes = COW_SECTION_SIZE * sizeof(uint64_t);

        for (i = 0; i < sect_size_bytes / COW_BLOCK_SIZE; i++) {
		int mapping_offset = (COW_BLOCK_SIZE / sizeof(cm->sects[sect_idx].mappings[0])) * i;
		int cow_file_offset = COW_BLOCK_SIZE * i;

        ret = file_write(cm->dfilp, cm->dev, cm->sects[sect_idx].mappings,
                         cm->sect_size * sect_idx * 8 + COW_HEADER_SIZE,
                         cm->sect_size * 8);
        if (ret) {
                LOG_ERROR(ret, "error writing cow manager section to file");
                return ret;
        }
        }

        return 0;
}

/**
 * __cow_sync_and_free_sections() - Used to synchronize and deallocate certain
 * sections from the &struct cow_manager.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 * @thresh: A threshold of zero will free all sections otherwise any section
 *          with a usage at or below the threshold will be synced and
 *          deallocated.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __cow_sync_and_free_sections(struct cow_manager *cm,
                                        unsigned long thresh)
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

/**
 * __cow_cleanup_mappings() - This deallocates sections from the
 * &struct cow_manager equal to approximately half of the cached sections.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __cow_cleanup_mappings(struct cow_manager *cm)
{
        unsigned long i;
        int ret;
        unsigned long granularity, thresh = 0;

        // find the max usage of the sections of the cow manager
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

/**
 * __cow_write_header() - Transfers in-memory header data to the header stored
 * on the block device.
 *
 * @cm: Each &struct snap_device has a &struct cow_manager.
 * @is_clean: Used to indicate whether the COW file has been closed correctly.
 * * 0: clears the COW_CLEAN flag.
 * * !0: sets the COW_CLEAN flag.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __cow_write_header(struct cow_manager *cm, int is_clean)
{
        int ret;
        struct cow_header ch;

        if (is_clean){
                cm->flags |= (1 << COW_CLEAN);
                LOG_DEBUG("writing COW header CLEAN");
        }
        else{
                cm->flags &= ~(1 << COW_CLEAN);
                LOG_DEBUG("writing COW header DIRTY");
        }

        ch.magic = COW_MAGIC;
        ch.flags = cm->flags;
        ch.fpos = cm->curr_pos;
        ch.fsize = cm->file_size;
        ch.seqid = cm->seqid;
        memcpy(ch.uuid, cm->uuid, COW_UUID_SIZE);
        ch.version = cm->version;
        ch.nr_changed_blocks = cm->nr_changed_blocks;

        ret = file_write(cm->dfilp, cm->dev, &ch, 0, sizeof(struct cow_header));
        if (ret) {
                LOG_ERROR(ret, "error syncing cow manager header");
                return ret;
        }

        return 0;
}

/**
 * __cow_open_header() - Reads and validates the &struct cow_header from the
 * beginning of the COW file. Then writes the header back to the backing file
 * to reflect and changes stored in the &struct cow_manager.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 * @index_only: int encoded bool indicating whether the COW file should be in
 *              incremental or snapshot mode?
 * @reset_vmalloc: int encoded bool indicating whether the COW_VMALLOC_UPPER
 *                 flag should be cleared.  The memory allocated for
 *                 &cow_manager->sects may be allocated by different allocators
 *                 and this presence or lack of this flag indicates how it
 *                 should be freed.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __cow_open_header(struct cow_manager *cm, int index_only, int reset_vmalloc)
{
        int ret;
        struct cow_header ch;

        ret = file_read(cm->dfilp, cm->dev, &ch, 0, sizeof(struct cow_header));
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
        cm->file_size = ch.fsize;
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

/**
 * cow_free_members() - Frees COW state tracking memory and unlinks the COW
 * backing file.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 */
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

        if (cm->dfilp) {
                file_unlink_and_close_force(cm->dfilp);
                __close_and_destroy_dattobd_mutable_file(cm->dfilp);
                cm->dfilp = NULL;
        }
}

/**
 * cow_free() - Frees the memory used for COW tracking and unlinks the COW
 * backing file from the block device.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 */
void cow_free(struct cow_manager *cm)
{
        cow_free_members(cm);
        kfree(cm);
}

/**
 * cow_sync_and_free() - Flushes cached data to the backing file, closes the
 * COW backing file and deallocates the &struct cow_manager.
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int cow_sync_and_free(struct cow_manager *cm)
{
        int ret;

        LOG_DEBUG("ENTER cow_sync_and_free");
        ret = __cow_sync_and_free_sections(cm, 0);
        if (ret)
                goto error;

        ret = __cow_close_header(cm);
        if (ret)
                goto error;

        if (cm->dfilp){
                __close_and_destroy_dattobd_mutable_file(cm->dfilp);
                cm->dfilp = NULL;
        }

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

/**
 * cow_sync_and_close() - Flushes cached data to the backing file, closes the
 * COW backing file but does not deallocate the &struct cow_manager.
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int cow_sync_and_close(struct cow_manager *cm)
{
        int ret;

        LOG_DEBUG("ENTER cow_sync_and_close");

        ret = __cow_sync_and_free_sections(cm, 0);
        if (ret)
                goto error;

        ret = __cow_close_header(cm);
        if (ret)
                goto error;

        ret = cow_get_file_extents(cm->dev, cm->dfilp->filp);
	if(ret) goto error;

        if (cm->dfilp){
                __close_and_destroy_dattobd_mutable_file(cm->dfilp);
                cm->dfilp = NULL;
        }

        return 0;

error:
        LOG_ERROR(ret, "error while syncing and closing cow manager");
        cow_free_members(cm);
        return ret;
}

/**
 * cow_reopen() - Re-opens an existing COW file located at @pathname.
 *
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 * @pathname: The path of the COW file.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int cow_reopen(struct cow_manager *cm, const char *pathname)
{
        int ret;

        LOG_DEBUG("reopening cow file");
        ret = __open_dattobd_mutable_file(pathname, 0, &cm->dfilp);
        if (ret)
                goto error;

        LOG_DEBUG("opening cow header");
        ret = __cow_open_header(cm, (cm->flags & (1 << COW_INDEX_ONLY)), 0);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error reopening cow manager");
        if (cm->dfilp){
                __close_and_destroy_dattobd_mutable_file(cm->dfilp);
                cm->dfilp = NULL;
        }

        return ret;
}

/**
 * __cow_calculate_allowed_sects() - Estimates the total number of cow
 * sections that can fit within the allowed cache size.
 *
 * @cache_size: The number of bytes allowed for the cache.  The cache should
 *              be at least as large as the memory required for the array of
 *              &struct cow_section objects used to track data during
 *              snapshotting.
 * @total_sects: The number of sections currently allocated.
 *
 * Return:
 * The remaining sections that would fit within memory set aside for the cache.
 */
static unsigned long __cow_calculate_allowed_sects(unsigned long cache_size,
                                                   unsigned long total_sects)
{
        if (cache_size <= (total_sects * sizeof(struct cow_section)))
                return 0;
        else
                return (cache_size -
                        (total_sects * sizeof(struct cow_section))) /
                       (COW_SECTION_SIZE * 8);
}

/**
 * cow_reload() - Allocates a &struct cow_manager object and reloads it from
 *                data saved in the supplied COW file.  All cached sections
 *                are marked as having data which will trigger loading from
 *                disk for each data section.
 * @path: The path to the COW file.
 * @elements: typically the number of sectors on the block device.
 * @sect_size: The basic unit of size that the &struct cow_manager works with.
 * @cache_size: The amount of RAM dedicated to the data cache.
 * @index_only: int encoded bool indicating whether the COW file should be in
 *              incremental or snapshot mode?
 * @cm_out: The reloaded &struct cow_manager object.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
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
        ret = __open_dattobd_mutable_file(path, 0, &cm->dfilp);
        if (ret)
                goto error;

        cm->allocated_sects = 0;
        cm->sect_size = sect_size;
        cm->log_sect_pages = get_order(sect_size * sizeof(uint64_t));
        cm->total_sects =
                NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
        cm->allowed_sects =
                __cow_calculate_allowed_sects(cache_size, cm->total_sects);
        cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size * sizeof(uint64_t)));
        cm->auto_expand = NULL;

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
        if (cm->dfilp){
                __close_and_destroy_dattobd_mutable_file(cm->dfilp);
                cm->dfilp = NULL;
        }

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

/**
 * cow_init() - Allocates a &struct cow_manager object and initializes it.
 *              Also creates the COW backing file on disk and writes a
 *              header into it.
 * @dev:  The &struct snap_device that keeps snapshot device state.
 * @path: The path to the COW file.
 * @elements: typically the number of sectors on the block device.
 * @sect_size: The basic unit of size that the &struct cow_manager works with.
 * @cache_size: The amount of RAM dedicated to the data cache.
 * @file_max: The maximum size of the cow file.  It will be allocated to this
 *            size after it is created.
 * @uuid: NULL or a valid pointer to a UUID.
 * @seqid: The sequence ID used to identify the snapshot.
 * @cm_out: The initialized &struct cow_manager object.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int cow_init(struct snap_device *dev, const char *path, uint64_t elements, unsigned long sect_size,
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
        ret = __open_dattobd_mutable_file(path, O_CREAT | O_TRUNC, &cm->dfilp);
        if (ret)
                goto error;

        cm->version = COW_VERSION_CHANGED_BLOCKS;
        cm->nr_changed_blocks = 0;
        cm->flags = 0;
        cm->allocated_sects = 0;
        cm->file_size = file_max;
        cm->sect_size = sect_size; //how many elements(sectors) can section hold (in datastore); = 4096
        cm->seqid = seqid;
        // get_order(x) = ceil[log2(x / PAGE_SIZE)]
        cm->log_sect_pages = get_order(sect_size * 8); //PAGE_SIZE = 4096 bytes; log2[pages in section in index]; = log2(4096*8/4096) = 3
        // this implies that section uses 2^15 B in index => sector consists of 8 B of metadata in index
        // means how many pages do we need to store metadata of one section in index
        cm->total_sects =
                NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);  //total sections to store all of the sectors; = ceil(elements / 4096)
        cm->allowed_sects =
                __cow_calculate_allowed_sects(cache_size, cm->total_sects); //num of sections that can fit in cache apart from index
        cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size * 8)); // data offset in bytes, equals 4096 + [total_sects*4096*8](index size)
        cm->curr_pos = cm->data_offset / COW_BLOCK_SIZE;
        cm->dev = dev;
        cm->auto_expand = NULL;

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
        ret = file_allocate(cm->dfilp, cm->dev, 0, file_max, NULL);
        if (ret)
                goto error;

        ret = __cow_write_header_dirty(cm);
        if (ret)
                goto error;

        *cm_out = cm;
        return 0;

error:
        LOG_ERROR(ret, "error during cow manager initialization");
        if (cm->dfilp){
                file_unlink_and_close(cm->dfilp);
                __close_and_destroy_dattobd_mutable_file(cm->dfilp);
                cm->dfilp = NULL;
        }

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

/**
 * cow_truncate_to_index() - Truncates the COW file so that it only contains
 * the header and index.
 *
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int cow_truncate_to_index(struct cow_manager *cm)
{
        int ret;

        // truncate the cow file to just the index
        cm->flags |= (1 << COW_INDEX_ONLY);
        ret = file_truncate(cm->dfilp, cm->data_offset);

        if(!ret)
                cm->file_size = cm->data_offset;
        
        return ret;
}

/**
 * cow_modify_cache_size() - Modifies the value of
 *                           &struct cow_manager->allowed_sects.
 *
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 * @cache_size: The number of bytes allowed for the cache.  The cache should
 *              be at least as large as the memory required for the array of
 *              &struct cow_section objects used to track data during
 *              snapshotting.
 */
void cow_modify_cache_size(struct cow_manager *cm, unsigned long cache_size)
{
        cm->allowed_sects =
                __cow_calculate_allowed_sects(cache_size, cm->total_sects);
}

/**
 * cow_read_mapping() - Loads a section into &struct cow_manager cache.  If
 * the newly loaded section exceeds the number of allowed sections then the
 * cache is cleaned up to free up space.
 *
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 * @pos: The section index offset within the cache.
 * @out: On success, output of the value stored in the mapping.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
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

/**
 * __cow_write_mapping() - Writes the specified section to the COW file.
 *
 * @cm: The &struct cow_manager associated with the &struct snap_device.
 * @pos: The section index offset within the cache.
 * @val:
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __cow_write_mapping(struct cow_manager *cm, uint64_t pos, uint64_t val)
{
        int ret;
        uint64_t sect_idx = pos;
        unsigned long sect_pos = do_div(sect_idx, cm->sect_size);
        //do_div modifies sect_idx to be the quotient of pos divided by cm->sect_size and returns the remainder

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

/**
 * __cow_write_data() - Writes a block of COW data to the current position
 * in the COW file.
 *
 * @cm: each &struct snap_device has a &struct cow_manager.
 * @buf: A buffer at least as large as COW_BLOCK_SIZE.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __cow_write_data(struct cow_manager *cm, void *buf)
{
        int ret;
        char *abs_path = NULL;
        int abs_path_len;
        uint64_t curr_size = cm->curr_pos * COW_BLOCK_SIZE;
        uint64_t expand_allowance = 0;
        int kstatfs_ret;
        struct kstatfs kstatfs;

retry:
        if (curr_size >= cm->file_size) {
                // try expansion of cow_file
                if(cm->auto_expand){
                        kstatfs_ret = 0;
                        if(cm->dev && cm->dev->sd_base_dev){
                                kstatfs_ret = dattobd_get_kstatfs(cm->dev->sd_base_dev, &kstatfs);
                        }

                        if(!kstatfs_ret){
                                expand_allowance = cow_auto_expand_manager_get_allowance(cm->auto_expand, kstatfs.f_bavail, (uint64_t) kstatfs.f_bsize);
                        }else{
                                LOG_WARN("failed to get kstatfs with error code %d, expansion allowance is given only if reserved space is 0.", kstatfs_ret);
                                expand_allowance = cow_auto_expand_manager_get_allowance_free_unknown(cm->auto_expand);
                        }

                        if(expand_allowance){
                                ret = tracer_expand_cow_file_no_check(cm->dev, expand_allowance);
                                expand_allowance = 0;
                                if(ret)
                                        goto error;
                                goto retry;
                        }
                }


                ret = -EFBIG;

                file_get_absolute_pathname(cm->dfilp, &abs_path, &abs_path_len);
                if (!abs_path) {
                        LOG_ERROR(ret, "cow file max size exceeded (%llu/%llu)",
                                  curr_size, cm->file_size);
                } else {
                        LOG_ERROR(ret,
                                  "cow file '%s' max size exceeded (%llu/%llu)",
                                  abs_path, curr_size, cm->file_size);
                        kfree(abs_path);
                }

                goto error;
        }

        ret = file_write(cm->dfilp, cm->dev, buf, curr_size, COW_BLOCK_SIZE);
        if (ret)
                goto error;

        cm->curr_pos++;

        return 0;

error:
        LOG_ERROR(ret, "error writing cow data");
        return ret;
}

/**
 * cow_write_current() - Conditionally writes the @block data stored in @buf
 * to the cow datastore.  Writing is short circuited to prevent overwriting
 * snapshot data if something is already stored for this @block.  When not
 * already present both the mapping and the data are stored.
 *
 * @cm: each &struct snap_device has a &struct cow_manager
 * @block: the block associated with the data in @buf
 * @buf: The data belonging to the @block
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
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

/**
 * cow_read_data() - Reads data form the COW file.
 *
 * @cm: each &struct snap_device has a &struct cow_manager.
 * @buf: A buffer that must be at least @len bytes.
 * @block_pos: Reads at this block position.
 * @block_off: A block offset that can be less than a full COW block.
 * @len: How many bytes to read at the supplied location.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int cow_read_data(struct cow_manager *cm, void *buf, uint64_t block_pos,
                  unsigned long block_off, unsigned long len)
{
        int ret;

        if (block_off >= COW_BLOCK_SIZE)
                return -EINVAL;

        ret = file_read(cm->dfilp, cm->dev, buf, (block_pos * COW_BLOCK_SIZE) + block_off,
                        len);
        if (ret) {
                LOG_ERROR(ret, "error reading cow data");
                return ret;
        }

        return 0;
}

int cow_get_file_extents(struct snap_device* dev, struct file* filp)
{
	int ret;
	struct fiemap_extent_info fiemap_info;
	unsigned int fiemap_mapped_extents_size, i_ext;
	struct fiemap_extent *extent;
	char parent_process_name[TASK_COMM_LEN];
	unsigned long vm_flags = VM_READ | VM_WRITE;
	unsigned long start_addr;
	struct task_struct *task;
	struct vm_area_struct *vma;
	struct page *pg;
	__user uint8_t *cow_ext_buf;

        unsigned long cow_ext_buf_size = ALIGN(dattobd_cow_ext_buf_size, PAGE_SIZE);

        int (*fiemap)(struct inode *, struct fiemap_extent_info *, u64 start, u64 len);

        int (*insert_vm_struct)(struct mm_struct *mm, struct vm_area_struct *vma) = (INSERT_VM_STRUCT_ADDR != 0) ?
        (int (*)(struct mm_struct *mm, struct vm_area_struct *vma)) (INSERT_VM_STRUCT_ADDR + (long long)(((void *)kfree) - (void *)KFREE_ADDR)) : NULL;

        	if (!insert_vm_struct) {
		LOG_ERROR(-ENOTSUPP, "insert_vm_struct() was not found");
		return -ENOTSUPP;
	}

        fiemap = NULL;
	task = get_current();

        LOG_DEBUG("getting cow file extents from filp=%p", filp);
	LOG_DEBUG("attempting page stealing from %s", get_task_comm(parent_process_name, task));

        dattobd_mm_lock(task->mm);
        start_addr = get_unmapped_area(NULL, 0, cow_ext_buf_size, 0, VM_READ | VM_WRITE);

        if (IS_ERR_VALUE(start_addr))
		return start_addr; // returns -EPERM if failed


        vma = dattobd_vm_area_allocate(task->mm);

	if (!vma) {
		ret = -ENOMEM;
		LOG_ERROR(ret, "vm_area_alloc() failed");
		dattobd_mm_unlock(task->mm);
		return ret;
	}

        vma->vm_start = start_addr;
	vma->vm_end = start_addr + cow_ext_buf_size;
	*(unsigned long *) &vma->vm_flags = vm_flags;
	vma->vm_page_prot = vm_get_page_prot(vm_flags);
	vma->vm_pgoff = 0;

        ret = insert_vm_struct(task->mm, vma);
        if (ret < 0) {
		ret = -EINVAL;
		LOG_ERROR(ret, "insert_vm_struct() failed");
		dattobd_vm_area_free(vma);
		dattobd_mm_unlock(task->mm);
		return ret;
	}

        pg = alloc_pages(GFP_USER, get_order(cow_ext_buf_size));
	if (!pg) {
		ret = -ENOMEM;
		LOG_ERROR(ret, "alloc_page() failed");
		dattobd_vm_area_free(vma);
		dattobd_mm_unlock(task->mm);
		return ret;
	}

        SetPageReserved(pg);
	ret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(pg), cow_ext_buf_size, PAGE_SHARED);
	if (ret < 0) {
		LOG_ERROR(ret, "remap_pfn_range() failed");
		ClearPageReserved(pg);
		__free_pages(pg, get_order(cow_ext_buf_size));
		dattobd_vm_area_free(vma);
		dattobd_mm_unlock(task->mm);
		return ret;
	}

        cow_ext_buf = (__user uint8_t *) start_addr;

	if (filp->f_inode->i_op)
		fiemap = filp->f_inode->i_op->fiemap;

        if (fiemap) {
		int64_t fiemap_max = ~0ULL & ~(1ULL << 63);
		int max_num_extents = cow_ext_buf_size; // used for do_div() as it overwrites the first argument

		fiemap_info.fi_flags = FIEMAP_FLAG_SYNC;
		fiemap_info.fi_extents_mapped = 0;
		do_div(max_num_extents, sizeof(struct fiemap_extent));
		fiemap_info.fi_extents_max = max_num_extents;
		fiemap_info.fi_extents_start = (struct fiemap_extent __user *)cow_ext_buf;

		ret = fiemap(filp->f_inode, &fiemap_info, 0, fiemap_max);

		LOG_DEBUG("fiemap for cow file (ret %d), extents %u (max %u)", ret,
				fiemap_info.fi_extents_mapped, fiemap_info.fi_extents_max);

		if (!ret && fiemap_info.fi_extents_mapped > 0) {
			if (dev->sd_cow_extents) kfree(dev->sd_cow_extents);
			fiemap_mapped_extents_size = fiemap_info.fi_extents_mapped * sizeof(struct fiemap_extent);
			dev->sd_cow_extents = kmalloc(fiemap_mapped_extents_size, GFP_KERNEL);
			if (dev->sd_cow_extents) {
                                //TODO: closely watch
				ret = copy_from_user(dev->sd_cow_extents, cow_ext_buf, fiemap_mapped_extents_size);
				if (!ret) {
					dev->sd_cow_ext_cnt = fiemap_info.fi_extents_mapped;
					WARN(dev->sd_cow_ext_cnt == max_num_extents, "max num of extents read, increase cow_ext_buf_size");
					extent = dev->sd_cow_extents;
					for (i_ext = 0; i_ext < fiemap_info.fi_extents_mapped; ++i_ext, ++extent) {
						LOG_DEBUG("   cow file extent: log 0x%llx, phy 0x%llx, len %llu", extent->fe_logical, extent->fe_physical, extent->fe_length);
					}
				}
			}
		}
	} else {
		ret = -ENOTSUPP;
		LOG_ERROR(ret, "fiemap not supported");
		goto out;
	}

out:
	ClearPageReserved(pg);
	dattobd_mm_unlock(task->mm);
	vm_munmap(vma->vm_start, cow_ext_buf_size);
	__free_pages(pg, get_order(cow_ext_buf_size));
	return ret;
}


int __cow_expand_datastore(struct cow_manager* cm, uint64_t append_size_bytes){
        int ret;
        uint64_t actual = 0;

        LOG_DEBUG("trying to expand cow file with %llu bytes", append_size_bytes);

        ret = file_allocate(cm->dfilp, cm->dev, cm->file_size, append_size_bytes, &actual);

        if(actual != append_size_bytes){
                LOG_WARN("cow file was not expanded to requested size (req: %llu, act: %llu)", append_size_bytes, actual);
        }

        cm->file_size = cm->file_size + actual;

        if (ret){
                LOG_ERROR(ret, "unable to expand cow file");
                return ret;
        }

        return 0;
}

struct cow_auto_expand_manager* cow_auto_expand_manager_init(void){
        struct cow_auto_expand_manager* aem = kzalloc(sizeof(struct cow_auto_expand_manager), GFP_KERNEL);
        if(!aem){
                LOG_ERROR(-ENOMEM, "error allocating cow auto expand manager");
                return ERR_PTR(-ENOMEM);
        }

        mutex_init(&aem->lock);

        return aem;
}

int cow_auto_expand_manager_reconfigure(struct cow_auto_expand_manager* aem, uint64_t step_size_mib, uint64_t reserved_space_mib){
        mutex_lock(&aem->lock);
        aem->step_size_mib = step_size_mib;
        aem->reserved_space_mib = reserved_space_mib;
        mutex_unlock(&aem->lock);
        return 0;
}

/*
* cow_auto_expand_manager_get_allowance() - Tests if the auto expand manager has steps remaining regarding to the available blocks and block size.
*
* @aem: The &struct cow_auto_expand_manager.
* @available_blocks: The number of available blocks on the block device. (from kstatfs, f_bavail)
* @block_size: The size of a block on the block device. (from kstatfs, f_bsize)
*
* Return:
* 0 - no steps remaining
* !0 - size to expand the cow file by
*/
uint64_t cow_auto_expand_manager_get_allowance(struct cow_auto_expand_manager* aem, uint64_t available_blocks, uint64_t block_size_bytes){
        #define ceil(a, b) (((a)+(b)-1)/(b))
        #define mib_to_bytes(a) ((a)*1024*1024)
        
        uint64_t ret;

        ret = 0;
        mutex_lock(&aem->lock);
        if(aem->step_size_mib && ceil(mib_to_bytes(aem->step_size_mib + aem->reserved_space_mib), block_size_bytes) <= available_blocks){
                ret = mib_to_bytes(aem->step_size_mib);
        }else{
                if(aem->step_size_mib){
                        LOG_WARN("rejected auto-expand: %llu MiB step size, %llu MiB reserved space, %llu blocks available, %llu B block size",
                                aem->step_size_mib, aem->reserved_space_mib, available_blocks, block_size_bytes);
                }
        }
        mutex_unlock(&aem->lock);

        return ret;
}

/*
* cow_auto_expand_manager_get_allowance_free_unknown() - Tests if the auto expand manager has steps remaining if the free space is not available.
* Allows to make an auto-expand if the reserved space is 0.
*
* @aem: The &struct cow_auto_expand_manager.
*
* Return:
* 0 - no steps remaining
* !0 - size to expand the cow file by
*/
uint64_t cow_auto_expand_manager_get_allowance_free_unknown(struct cow_auto_expand_manager* aem){
        #define mib_to_bytes(a) ((a)*1024*1024)
        
        uint64_t ret;

        ret = 0;
        mutex_lock(&aem->lock);
        // We allow COW-File Auto-Expansion when free space is unknown only for cases where reserved space is 0
        if(aem->step_size_mib && aem->reserved_space_mib == 0){
                ret = mib_to_bytes(aem->step_size_mib);
        }
        mutex_unlock(&aem->lock);

        return ret;
}


void cow_auto_expand_manager_free(struct cow_auto_expand_manager* aem){
        mutex_destroy(&aem->lock);
        kfree(aem);
}