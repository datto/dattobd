// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

#include "includes.h"
#include "userspace_copy_helpers.h"
#include "snap_device.h"

#define file_write(filp, dev, buf, offset, len) file_io(filp, dev, 1, buf, offset, len)
#define file_read(filp, dev, buf, offset, len) file_io(filp, dev, 0, buf, offset, len)

#define file_unlink(filp) __file_unlink(filp, 0, 0)
#define file_unlink_and_close(filp) __file_unlink(filp, 1, 0)
#define file_unlink_and_close_force(filp) __file_unlink(filp, 1, 1)

#define file_lock(filp) file_switch_lock(filp, true, false)
#define file_unlock(filp) file_switch_lock(filp, false, false)
#define file_unlock_mark_dirty(filp) file_switch_lock(filp, false, true)

#ifndef HAVE_STRUCT_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#define dattobd_get_dentry(f) (f)->f_dentry
#define dattobd_get_mnt(f) (f)->f_vfsmnt
#else
#define dattobd_get_dentry(f) (f)->f_path.dentry
#define dattobd_get_mnt(f) (f)->f_path.mnt
#endif

#ifndef HAVE_PATH_PUT
#define dattobd_d_path(path, page_buf, page_size)                              \
        d_path((path)->dentry, (path)->mnt, page_buf, page_size)
#define dattobd_get_nd_dentry(nd) (nd).dentry
#define dattobd_get_nd_mnt(nd) (nd).mnt
#else
#define dattobd_d_path(path, page_buf, page_size)                              \
        d_path(path, page_buf, page_size)
#define dattobd_get_nd_dentry(nd) (nd).path.dentry
#define dattobd_get_nd_mnt(nd) (nd).path.mnt
#endif

// takes a value and the log of the value it should be rounded up to
#define NUM_SEGMENTS(x, log_size) (((x) + (1 << (log_size)) - 1) >> (log_size))
#define SECTOR_INVALID ~(u64)0

struct file;
struct dentry;
struct vfsmount;

#ifndef HAVE_STRUCT_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
struct path {
        struct vfsmount *mnt;
        struct dentry *dentry;
};
#endif

#ifndef HAVE_FMODE_T
typedef mode_t fmode_t;
#endif

int file_io(struct file *filp, struct snap_device* dev, int is_write, void *buf, sector_t offset,
            unsigned long len);

void file_close(struct file *f);

int file_open(const char *filename, int flags, struct file **filp);

#if !defined(HAVE___DENTRY_PATH) && !defined(HAVE_DENTRY_PATH_RAW)
int dentry_get_relative_pathname(struct dentry *dentry, char **buf,
                                 int *len_res);
#else
int dentry_get_relative_pathname(struct dentry *dentry, char **buf,
                                 int *len_res);
#endif

int file_get_absolute_pathname(const struct file *filp, char **buf,
                               int *len_res);

int pathname_to_absolute(const char *pathname, char **buf, int *len_res);

int pathname_concat(const char *pathname1, const char *pathname2,
                    char **path_out);

int user_mount_pathname_concat(const char __user *user_mount_path,
                               const char *rel_path, char **path_out);

int file_truncate(struct file *filp, loff_t len);

int file_allocate(struct file *filp, struct snap_device* dev, uint64_t offset, uint64_t length);

int __file_unlink(struct file *filp, int close, int force);

#ifndef HAVE_NOOP_LLSEEK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
loff_t noop_llseek(struct file *file, loff_t offset, int origin);
#endif

#ifndef HAVE_PATH_PUT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
void path_put(const struct path *path);
#endif

#ifndef HAVE_INODE_LOCK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
void dattobd_inode_lock(struct inode *inode);

void dattobd_inode_unlock(struct inode *inode);
#else
#define dattobd_inode_lock inode_lock
#define dattobd_inode_unlock inode_unlock
#endif

struct vm_area_struct* dattobd_vm_area_allocate(struct mm_struct* mm);

void dattobd_vm_area_free(struct vm_area_struct *vma);

void dattobd_mm_lock(struct mm_struct* mm);

void dattobd_mm_unlock(struct mm_struct* mm);

void file_switch_lock(struct file* filp, bool lock, bool mark_dirty);

int file_write_block(struct snap_device* dev, const void* block, size_t offset, size_t len);

int file_read_block(struct snap_device* dev, void* block, size_t offset, size_t len);

sector_t sector_by_offset(struct snap_device*dev, size_t offset);

#endif /* FILESYSTEM_H_ */
