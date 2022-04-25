// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "task_helper.h"

#include "hints.h"
#include "includes.h"

#ifdef HAVE_TASK_STRUCT_TASK_WORKS_HLIST

/**
 * task_work_flush() - Each entry in the &task_struct->task_works field for the
 *                     current process has its func() called.  The list is
 *                     processed in reverse order.  This is a reimplementation
 *                     of task_work_run(), called before returning to user
 *                     space, to force fput() and mntput() to perform their
 *                     work synchronously.
 */
void task_work_flush(void)
{
        struct task_struct *task = current;
        struct hlist_head task_works;
        struct hlist_node *pos;

        raw_spin_lock_irq(&task->pi_lock);
        hlist_move_list(&task->task_works, &task_works);
        raw_spin_unlock_irq(&task->pi_lock);

        if (unlikely(hlist_empty(&task_works)))
                return;

        for (pos = task_works.first; pos->next; pos = pos->next)
                ;

        for (;;) {
                struct hlist_node **pprev = pos->pprev;
                struct task_work *twork =
                        container_of(pos, struct task_work, hlist);
                twork->func(twork);

                if (pprev == &task_works.first)
                        break;
                pos = container_of(pprev, struct hlist_node, next);
        }
}
#elif defined HAVE_TASK_STRUCT_TASK_WORKS_CB_HEAD

/**
 * task_work_flush() - Each entry in the &task_struct->task_works field for the
 *                     current process has its func() called.  The list is
 *                     processed in reverse order.  This is a reimplementation
 *                     of task_work_run(), called before returning to user
 *                     space, to force fput() and mntput() to perform their
 *                     work synchronously.
 */
void task_work_flush(void)
{
        struct task_struct *task = current;
        struct callback_head *work, *head, *next;

        for (;;) {
                do {
                        work = ACCESS_ONCE(task->task_works);
                        head = NULL; // current should not be PF_EXITING
                } while (cmpxchg(&task->task_works, work, head) != work);

                if (!work)
                        break;

                raw_spin_lock_irq(&task->pi_lock);
                raw_spin_unlock_irq(&task->pi_lock);

                head = NULL;
                do {
                        next = work->next;
                        work->next = head;
                        head = work;
                        work = next;
                } while (work);

                work = head;
                do {
                        next = work->next;
                        work->func(work);
                        work = next;
                        cond_resched();
                } while (work);
        }
}

#else

#define task_work_flush()

#endif
