/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "includes.h"
#include "kernel-config.h"

void file_close(struct file *f);
int file_open(const char *filename, int flags, struct file **filp);
int file_io(struct file *filp, int is_write, void *buf, sector_t offset, unsigned long len);
#define file_write(filp, buf, offset, len) file_io(filp, 1, buf, offset, len)
#define file_read(filp, buf, offset, len) file_io(filp, 0, buf, offset, len)

int file_truncate(struct file *filp, loff_t len);

#ifdef HAVE_VFS_FALLOCATE
	#define real_fallocate(f, offset, length) vfs_fallocate(f, 0, offset, length)
#else
int real_fallocate(struct file *f, uint64_t offset, uint64_t length);
#endif

int file_allocate(struct file *f, uint64_t offset, uint64_t length);

int __file_unlink(struct file *filp, int close, int force);
#define file_unlink(filp) __file_unlink(filp, 0, 0)
#define file_unlink_and_close(filp) __file_unlink(filp, 1, 0)
#define file_unlink_and_close_force(filp) __file_unlink(filp, 1, 1)

int dentry_get_relative_pathname(struct dentry *dentry, char **buf, int *len_res);
int path_get_absolute_pathname(struct path *path, char **buf, int *len_res);
int pathname_to_absolute(char *pathname, char **buf, int *len_res);
int pathname_concat(char *pathname1, char *pathname2, char **path_out);
int user_mount_pathname_concat(char __user *user_mount_path, char *rel_path, char **path_out);
