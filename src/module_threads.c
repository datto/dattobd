// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "module_threads.h"
#include "bio_helper.h"
#include "bio_queue.h"
#include "cow_manager.h"
#include "logging.h"
#include "mrf.h"
#include "snap_device.h"
#include "snap_handle.h"
#include "sset_queue.h"
#include "submit_bio.h"
#include "tracer_helper.h"

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/mm.h>
#endif

// this is defined in 3.16 and up
#ifndef MIN_NICE
#define MIN_NICE -20
#endif

/**
 * thread_noio_enter() - Keeps the calling COW/sset kernel thread from
 * recursing into I/O during memory reclaim.
 *
 * These threads persist data by writing to a file on the traced block device.
 * If one of their allocations enters direct reclaim and reclaim writes pages
 * back (e.g. to a swapfile on the same filesystem), that write is traced and
 * needs a read clone from the device's bioset. Those clones are only given
 * back by these very threads, so the thread ends up waiting on itself in
 * mempool_alloc() and every writer on the device hangs. The loop driver
 * guards against the same recursion in the same way.
 *
 * Return: the previous noio state, to be passed to thread_noio_exit().
 */
static unsigned int thread_noio_enter(void)
{
        unsigned int noio_flags = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
        noio_flags = memalloc_noio_save();
#endif
#if defined(PF_LOCAL_THROTTLE)
        // throttle dirtying only against our own writes, like loop does
        current->flags |= PF_LOCAL_THROTTLE;
#elif defined(PF_LESS_THROTTLE)
        current->flags |= PF_LESS_THROTTLE;
#endif
        return noio_flags;
}

/**
 * thread_noio_exit() - Restores the state saved by thread_noio_enter().
 * @noio_flags: the value returned by thread_noio_enter().
 */
static void thread_noio_exit(unsigned int noio_flags)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
        memalloc_noio_restore(noio_flags);
#endif
}

/**
 * cow_reserve_enter() - Lets the caller allocate from the memory reserves
 * without entering reclaim while it handles one queued request.
 *
 * Without I/O, reclaim can't free anything once clean pages are gone, so the
 * COW thread would spin in direct reclaim while every other writer waits for
 * it to give back read clones. Writes to the COW file itself are not traced,
 * so handling a request turns reserve pages into dirty COW-file pages that
 * writeback can clean without dattobd, and frees the request's clone pages.
 *
 * Return: the previous PF_MEMALLOC state, for cow_reserve_exit().
 */
static unsigned int cow_reserve_enter(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
        return memalloc_noreclaim_save();
#else
        unsigned int old = current->flags & PF_MEMALLOC;

        current->flags |= PF_MEMALLOC;
        return old;
#endif
}

/**
 * cow_reserve_exit() - Restores the state saved by cow_reserve_enter().
 * @flags: the value returned by cow_reserve_enter().
 */
static void cow_reserve_exit(unsigned int flags)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
        memalloc_noreclaim_restore(flags);
#else
        current->flags = (current->flags & ~PF_MEMALLOC) | flags;
#endif
}

/**
 * inc_sset_thread() - The thread entry that dequeues sector sets
 *                     and hands them off for processing.
 * @data: the &struct snap_device object pointer.
 *
 * If there is an error the queue is cleaned up and all ssets remaining are
 * freed without further processing.
 *
 * Return: always zero.
 */
int inc_sset_thread(void *data)
{
        int ret, is_failed = 0;
        struct snap_device *dev = data;
        struct sset_queue *sq = &dev->sd_pending_ssets;
        struct sector_set *sset;
        unsigned int noio_flags = thread_noio_enter();
        unsigned int reserve_flags;

        // give this thread the highest priority we are allowed
        set_user_nice(current, MIN_NICE);

        while (!kthread_should_stop() || !sset_queue_empty(sq)) {
                // wait for a sset to process or a kthread_stop call
                wait_event_interruptible(sq->event,
                                         kthread_should_stop() ||
                                                 !sset_queue_empty(sq));

                if (!is_failed && tracer_read_fail_state(dev)) {
                        LOG_DEBUG(
                                "error detected in sset thread, cleaning up cow");
                        is_failed = 1;

                        if (dev->sd_cow)
                                cow_free_members(dev->sd_cow);
                }

                if (sset_queue_empty(sq))
                        continue;

                // safely dequeue a sset
                sset = sset_queue_dequeue(sq);

                // if there has been a problem don't process any more, just free
                // the ones we have
                if (is_failed) {
                        kfree(sset);
                        continue;
                }

                // pass the sset to the handler
                reserve_flags = cow_reserve_enter();
                ret = inc_handle_sset(dev, sset);
                cow_reserve_exit(reserve_flags);
                if (ret) {
                        LOG_ERROR(ret,
                                  "error handling sector set in kernel thread");
                        tracer_set_fail_state(dev, ret);
                }

                // free the sector set
                kfree(sset);
        }

        thread_noio_exit(noio_flags);
        return 0;
}

/**
 * snap_cow_thread() - Processes BIOs by passing each to an appropriate
 * read or write handler.
 *
 * @data: The &struct snap_device object pointer.
 *
 * Return: always zero.
 */
int snap_cow_thread(void *data)
{
        int ret, is_failed = 0;
        struct snap_device *dev = data;
        struct bio_queue *bq = &dev->sd_cow_bios;
        struct bio *bio;
        unsigned int noio_flags = thread_noio_enter();
        unsigned int reserve_flags;

        // give this thread the highest priority we are allowed
        set_user_nice(current, MIN_NICE);

        while (!kthread_should_stop() || !bio_queue_empty(bq) || (atomic64_read(&dev->sd_submitted_cnt) != atomic64_read(&dev->sd_received_cnt) && !is_failed)) { 
                // wait for a bio to process or a kthread_stop call
                wait_event_interruptible(bq->event,
                                         kthread_should_stop() ||
                                                 !bio_queue_empty(bq));
               
                if (!is_failed && tracer_read_fail_state(dev)) {
                        LOG_DEBUG(
                                "error detected in cow thread, cleaning up cow");
                        is_failed = 1;

                        if (dev->sd_cow)
                                cow_free_members(dev->sd_cow);
                }

                if (bio_queue_empty(bq))
                        continue;

                // safely dequeue a bio
                bio = bio_queue_dequeue_delay_read(bq);

                // pass bio to handler
                if (!bio_data_dir(bio)) {
                        // if we're in the fail state just send back an IO error
                        // and free the bio
                        if (is_failed) {
                                dattobd_bio_endio(bio,
                                                  -EIO); // end the bio with an
                                                         // IO error
                                continue;
                        }

                        reserve_flags = cow_reserve_enter();
                        ret = snap_handle_read_bio(dev, bio);
                        cow_reserve_exit(reserve_flags);
                        if (ret) {
                                LOG_ERROR(
                                        ret,
                                        "error handling read bio in kernel thread");
                                tracer_set_fail_state(dev, ret);
                        }

                        dattobd_bio_endio(bio, (ret) ? -EIO : 0);
                } else {
                        if (is_failed) {
                                bio_free_clone(bio);
                                continue;
                        }

                        reserve_flags = cow_reserve_enter();
                        ret = snap_handle_write_bio(dev, bio);
                        cow_reserve_exit(reserve_flags);
                        if (ret) {
                                LOG_ERROR(ret, "error handling write bio in "
                                               "kernel thread");
                                tracer_set_fail_state(dev, ret);
                        }

                        bio_free_clone(bio);
                }
        }

        thread_noio_exit(noio_flags);
        return 0;
}

/**
 * snap_mrf_thread() - Dispatches the original BIOs to the block IO layer
 * one-by-one.
 *
 * @data: The &struct snap_device object pointer.
 *
 * Return: always zero.
 */
int snap_mrf_thread(void *data)
{
        int ret = 0;
        struct snap_device *dev = data;
        struct bio_queue *bq = &dev->sd_orig_bios;
        struct bio *bio = NULL;

        MAYBE_UNUSED(ret);

        // give this thread the highest priority we are allowed
        set_user_nice(current, MIN_NICE);

        while (!kthread_should_stop() || !bio_queue_empty(bq)) {
                // wait for a bio to process or a kthread_stop call
                wait_event_interruptible(bq->event,
                                         kthread_should_stop() ||
                                                 !bio_queue_empty(bq));
                if (bio_queue_empty(bq))
                        continue;

                // safely dequeue a bio
                bio = bio_queue_dequeue(bq);

                SUBMIT_BIO_REAL(dev,bio);
#ifdef HAVE_MAKE_REQUEST_FN_INT
                if (ret)
                        generic_make_request(bio);
#endif
        }

        return 0;
}
