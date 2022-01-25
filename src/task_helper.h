// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef TASK_WORK_HELPER_H_
#define TASK_WORK_HELPER_H_

/*****************************TASK WORK FUNCTIONS****************************/

// reimplementation of task_work_run() to force fput() and mntput() to perform
// their work synchronously
#ifdef HAVE_TASK_STRUCT_TASK_WORKS_HLIST
void task_work_flush(void);
#elif defined HAVE_TASK_STRUCT_TASK_WORKS_CB_HEAD
void task_work_flush(void);
#else
#define task_work_flush()
#endif

#endif /* TASK_WORK_HELPER_H_ */
