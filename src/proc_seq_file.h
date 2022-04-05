// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef PROC_SEQ_FILE_H_
#define PROC_SEQ_FILE_H_

#ifndef HAVE_PROC_OPS
const struct file_operations *get_proc_fops(void);
#else
const struct proc_ops *get_proc_fops(void);
#endif

#endif /* PROC_SEQ_FILE_H_ */
