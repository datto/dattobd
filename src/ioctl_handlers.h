// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef IOCTL_HANDLERS_H_
#define IOCTL_HANDLERS_H_

struct file;

extern struct mutex ioctl_mutex;

/************************IOCTL HANDLER FUNCTIONS************************/

long ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#endif /* IOCTL_HANDLERS_H_ */
