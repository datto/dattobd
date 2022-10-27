// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "filesystem.h"
#include "includes.h"
#include "logging.h"
#include "userspace_copy_helpers.h"

// if this isn't defined, we don't need it anyway
#ifndef FMODE_NONOTIFY
#define FMODE_NONOTIFY 0
#endif

#ifndef HAVE_MNT_WANT_WRITE
#define mnt_want_write(x) 0
#define mnt_drop_write (void)sizeof
#endif

#ifndef HAVE_KERN_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int kern_path(const char *name, unsigned int flags, struct path *path)
{
        struct nameidata nd;
        int ret = path_lookup(name, flags, &nd);
        if (!ret) {
                path->dentry = dattobd_get_nd_dentry(nd);
                path->mnt = dattobd_get_nd_mnt(nd);
        }
        return ret;
}
#endif

/**
 * dattobd_kernel_read() - This is a wrapper around kernel_read enhanced for
 * systems that don't support it.
 * @filp: A &struct file object.
 * @buf: A buffer with at least @count entries.
 * @count: The number of bytes to read from @filp.
 * @pos: Set to the offset into @filp identifying the first sequential access.
 *       Contains the current file offset after a successful call.
 *
 * Return: The number of bytes read or a negative errno.
 */
static ssize_t dattobd_kernel_read(struct file *filp, void *buf, size_t count,
                                   loff_t *pos)
{
#ifndef HAVE_KERNEL_READ_PPOS
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        mm_segment_t old_fs;
        ssize_t ret;

        old_fs = get_fs();
        set_fs(get_ds());
        ret = vfs_read(filp, (char __user *)buf, count, pos);
        set_fs(old_fs);

        return ret;
#else
        return kernel_read(filp, buf, count, pos);
#endif
}

/**
 * dattobd_kernel_write() - This is a wrapper around kernel_write enhanced for
 * systems that don't support it.
 * @filp: A &struct file object.
 * @buf: A buffer with at least @count entries.
 * @count: The number of bytes to write to @filp.
 * @pos: Set to the offset into @filp identifying the first sequential access.
 *       Contains the current file offset after a successful call.
 *
 * Return: The number of bytes written or a negative errno.
 */
static ssize_t dattobd_kernel_write(struct file *filp, const void *buf,
                                    size_t count, loff_t *pos)
{
#ifndef HAVE_KERNEL_WRITE_PPOS
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        mm_segment_t old_fs;
        ssize_t ret;

        old_fs = get_fs();
        set_fs(get_ds());
        ret = vfs_write(filp, (__force const char __user *)buf, count, pos);
        set_fs(old_fs);

        return ret;
#else
        return kernel_write(filp, buf, count, pos);
#endif
}

/**
 * file_io() - Reads or writes to the supplied file.
 *
 * @filp: A pointer to the file object.
 * @is_write: An integer encoded bool indicating a write or read operation.
 * @buf: Input/output buffer for write/read, respectively.
 * @offset: Byte offset of the first sequential access within @filp.
 * @len: The number of bytes in the transfer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int file_io(struct file *filp, int is_write, void *buf, sector_t offset,
            unsigned long len)
{
        ssize_t ret;
        loff_t off = (loff_t)offset;

        if (is_write)
                ret = dattobd_kernel_write(filp, buf, len, &off);
        else
                ret = dattobd_kernel_read(filp, buf, len, &off);

        if (ret < 0) {
                LOG_ERROR((int)ret, "error performing file '%s': %llu, %lu",
                          (is_write) ? "write" : "read",
                          (unsigned long long)offset, len);
                return ret;
        } else if (ret != len) {
                LOG_ERROR(-EIO, "invalid file '%s' size: %llu, %lu, %lu",
                          (is_write) ? "write" : "read",
                          (unsigned long long)offset, len, (unsigned long)ret);
                ret = -EIO;
                return ret;
        }

        return 0;
}

/**
 * file_write() - Writes @len bytes of data to offset @offset within @filp from
 * @buf.
 *
 * @filp: A pointer to the file object.
 * @buf: Input buffer for write.
 * @offset: Byte offset of the first sequential access within @filp.
 * @len: The number of bytes in the transfer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
#define file_write(filp, buf, offset, len) file_io(filp, 1, buf, offset, len)

/**
 * file_read() - Store @len bytes of data from offset @offset within @filp into
 * @buf.
 *
 * @filp: A pointer to the file object.
 * @buf: Output buffer for read.
 * @offset: Byte offset of the first sequential access within @filp.
 * @len: The number of bytes in the transfer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
#define file_read(filp, buf, offset, len) file_io(filp, 0, buf, offset, len)

/**
 * file_close() - Closes the file object.
 *
 * @f: A pointer to a file object.
 */
inline void file_close(struct file *f)
{
        filp_close(f, NULL);
}

/**
 * file_open() - Opens a file.
 *
 * @filename: The full path to a file.
 * @flags: Additional flags to use when opening the file.
 * @filp: The resultant file object pointer, if successful.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int file_open(const char *filename, int flags, struct file **filp)
{
        int ret;
        struct file *f;

        f = filp_open(filename, flags | O_RDWR | O_LARGEFILE, 0);
        if (!f) {
                ret = -EFAULT;
                LOG_ERROR(ret,
                          "error creating/opening file '%s' (null pointer)",
                          filename);
                goto error;
        } else if (IS_ERR(f)) {
                ret = PTR_ERR(f);
                f = NULL;
                LOG_ERROR(ret, "error creating/opening file '%s' - %d",
                          filename, ret);
                goto error;
        } else if (!S_ISREG(dattobd_get_dentry(f)->d_inode->i_mode)) {
                ret = -EINVAL;
                LOG_ERROR(ret, "'%s' is not a regular file", filename);
                goto error;
        }
        f->f_mode |= FMODE_NONOTIFY;

        *filp = f;
        return 0;

error:
        LOG_ERROR(ret, "error opening file");
        if (f)
                file_close(f);

        *filp = NULL;
        return ret;
}

#if !defined(HAVE___DENTRY_PATH) && !defined(HAVE_DENTRY_PATH_RAW)
/**
 * dentry_get_relative_pathname() - Returns the pathname of the supplied dentry
 * relative to the mount point of the block device containing the dentry.
 *
 * @dentry: A dentry object.
 * @buf: Output pathname. Use kfree() on the returned buffer.
 * @len_res: Pathname length of the result, NULL means don't care.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int dentry_get_relative_pathname(struct dentry *dentry, char **buf,
                                 int *len_res)
{
        int len = 0;
        char *pathname;
        struct dentry *parent = dentry;

        while (parent->d_parent != parent) {
                len += parent->d_name.len + 1;
                parent = parent->d_parent;
        }

        pathname = kmalloc(len + 1, GFP_KERNEL);
        if (!pathname) {
                LOG_ERROR(-ENOMEM, "error allocating pathname for dentry");
                return -ENOMEM;
        }
        pathname[len] = '\0';
        if (len_res)
                *len_res = len;
        *buf = pathname;

        parent = dentry;
        while (parent->d_parent != parent) {
                len -= parent->d_name.len + 1;
                pathname[len] = '/';
                strncpy(&pathname[len + 1], parent->d_name.name,
                        parent->d_name.len);
                parent = parent->d_parent;
        }

        return 0;
}
#else
/**
 * dentry_get_relative_pathname() - Returns the pathname of the supplied dentry
 * relative to the mount point of the block device containing the dentry.
 *
 * @dentry: A dentry object.
 * @buf: Output pathname. Use kfree() on the returned buffer.
 * @len_res: Pathname length of the result, NULL means don't care.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int dentry_get_relative_pathname(struct dentry *dentry, char **buf,
                                 int *len_res)
{
        int ret, len;
        char *pathname, *page_buf, *final_buf = NULL;

        page_buf = (char *)__get_free_page(GFP_KERNEL);
        if (!page_buf) {
                LOG_ERROR(-ENOMEM, "error allocating page for dentry pathname");
                return -ENOMEM;
        }

#ifdef HAVE___DENTRY_PATH
        spin_lock(&dcache_lock);
        pathname = __dentry_path(dentry, page_buf, PAGE_SIZE);
        spin_unlock(&dcache_lock);
#else
        pathname = dentry_path_raw(dentry, page_buf, PAGE_SIZE);
#endif
        if (IS_ERR(pathname)) {
                ret = PTR_ERR(pathname);
                pathname = NULL;
                LOG_ERROR(ret, "error fetching dentry pathname");
                goto error;
        }

        len = page_buf + PAGE_SIZE - pathname;
        final_buf = kmalloc(len, GFP_KERNEL);
        if (!final_buf) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating pathname for dentry");
                goto error;
        }

        strncpy(final_buf, pathname, len);
        free_page((unsigned long)page_buf);

        *buf = final_buf;
        if (len_res)
                *len_res = len;
        return 0;

error:
        LOG_ERROR(ret, "error converting dentry to relative path name");
        if (final_buf)
                kfree(final_buf);
        if (page_buf)
                free_page((unsigned long)page_buf);

        *buf = NULL;
        if (len_res)
                *len_res = 0;
        return ret;
}
#endif

/**
 * path_get_absolute_pathname() - Gets an absolute pathname from the supplied
 *                                &struct path object.
 *
 * @path: Contains dentry objects for both a directory and its mount point.
 * @buf: Output pathname. Use kfree() on the returned buffer.
 * @len_res: Pathname length of the result, NULL means don't care.
 *
 * The absolute path must be less than PAGE_SIZE bytes.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
static int path_get_absolute_pathname(const struct path *path, char **buf,
                                      int *len_res)
{
        int ret, len;
        char *pathname, *page_buf, *final_buf = NULL;

        page_buf = (char *)__get_free_page(GFP_KERNEL);
        if (!page_buf) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating page for absolute pathname");
                goto error;
        }

        pathname = dattobd_d_path(path, page_buf, PAGE_SIZE);
        if (IS_ERR(pathname)) {
                ret = PTR_ERR(pathname);
                pathname = NULL;
                LOG_ERROR(ret, "error fetching absolute pathname");
                goto error;
        }

        len = page_buf + PAGE_SIZE - pathname;
        final_buf = kmalloc(len, GFP_KERNEL);
        if (!final_buf) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating buffer for absolute pathname");
                goto error;
        }

        strncpy(final_buf, pathname, len);
        free_page((unsigned long)page_buf);

        *buf = final_buf;
        if (len_res)
                *len_res = len;
        return 0;

error:
        LOG_ERROR(ret, "error getting absolute pathname from path");
        if (final_buf)
                kfree(final_buf);
        if (page_buf)
                free_page((unsigned long)page_buf);

        *buf = NULL;
        if (len_res)
                *len_res = 0;
        return ret;
}

/**
 * file_get_absolute_pathname() - Gets an absolute path from the supplied
 *                                &struct file object.
 *
 * @filp: A &struct file object.
 * @buf: Output pathname. Use kfree() on the returned buffer.
 * @len_res: Pathname length of the result, NULL means don't care.
 *
 * This is a wrapper based on @path_get_absolute_pathname.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int file_get_absolute_pathname(const struct file *filp, char **buf,
                               int *len_res)
{
        struct path path;
        int ret;

        path.mnt = dattobd_get_mnt(filp);
        path.dentry = dattobd_get_dentry(filp);

        ret = path_get_absolute_pathname(&path, buf, len_res);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error converting file to absolute pathname");
        *buf = NULL;
        *len_res = 0;

        return ret;
}

/**
 * pathname_to_absolute() - Converts a pathname, relative or absolute, to
 *                          and absolute pathname.
 *
 * @pathname: The pathname to convert.
 * @buf: Output pathname. Use kfree() on the returned buffer.
 * @len_res: Pathname length of the result, NULL means don't care.
 *
 * This is a wrapper based on @path_get_absolute_pathname.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int pathname_to_absolute(const char *pathname, char **buf, int *len_res)
{
        int ret;
        struct path path = {};

        ret = kern_path(pathname, LOOKUP_FOLLOW, &path);
        if (ret) {
                LOG_ERROR(ret, "error finding path for pathname");
                return ret;
        }

        ret = path_get_absolute_pathname(&path, buf, len_res);
        if (ret)
                goto error;

        path_put(&path);
        return 0;

error:
        LOG_ERROR(ret, "error converting pathname to absolute pathname");
        path_put(&path);
        return ret;
}

/**
 * pathname_concat() - Appends @pathname2 to @pathname1.
 *
 * @pathname1: A pathname.
 * @pathname2: The pathname to append to @pathname1.
 * @path_out: Output pathname. Use kfree() on the returned buffer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int pathname_concat(const char *pathname1, const char *pathname2,
                    char **path_out)
{
        int pathname1_len, pathname2_len, need_leading_slash = 0;
        int kmalloc_len, offset;
        char *full_pathname;

        pathname1_len = strlen(pathname1);
        pathname2_len = strlen(pathname2);

        if (pathname1[pathname1_len - 1] != '/' && pathname2[0] != '/')
                need_leading_slash = 1;
        else if (pathname1[pathname1_len - 1] == '/' && pathname2[0] == '/')
                pathname1_len--;

        kmalloc_len = pathname1_len + pathname2_len + need_leading_slash + 1;
        full_pathname = kmalloc(kmalloc_len, GFP_KERNEL);
        if (!full_pathname) {
                LOG_ERROR(-ENOMEM,
                          "error allocating buffer for pathname concatenation");
                *path_out = NULL;
                return -ENOMEM;
        }
        full_pathname[pathname1_len + need_leading_slash + pathname2_len] =
                '\0';

        strncpy(full_pathname, pathname1, pathname1_len);
        if (need_leading_slash)
                full_pathname[pathname1_len] = '/';
        offset = pathname1_len + need_leading_slash;
        strncpy(full_pathname + offset, pathname2, kmalloc_len - offset - 1);

        *path_out = full_pathname;
        return 0;
}

/**
 * user_mount_pathname_concat() - Concatinates a relative path to a user space
 * mount path.
 *
 * @user_mount_path: A user space mount path, will be copied to kernel space.
 * @rel_path: A kernel space relative path.
 * @path_out: Output pathname. Use kfree() on the returned buffer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int user_mount_pathname_concat(const char __user *user_mount_path,
                               const char *rel_path, char **path_out)
{
        int ret;
        char *mount_path;

        ret = copy_string_from_user(user_mount_path, &mount_path);
        if (ret)
                goto error;

        ret = pathname_concat(mount_path, rel_path, path_out);
        if (ret)
                goto error;

        kfree(mount_path);
        return 0;

error:
        LOG_ERROR(ret, "error concatenating mount path to relative path");
        if (mount_path)
                kfree(mount_path);

        *path_out = NULL;
        return ret;
}

/**
 * dattobd_should_remove_suid() - Determines flags needed to remove suid.
 *
 * @dentry: A &struct dentry object pointer.
 *
 * Return: The necessary flags.
 */
static int dattobd_should_remove_suid(struct dentry *dentry)
{
        mode_t mode = dentry->d_inode->i_mode;
        int kill = 0;

        /* suid always must be killed */
        if (unlikely(mode & S_ISUID))
                kill = ATTR_KILL_SUID;

        /*
         * sgid without any exec bits is just a mandatory locking mark; leave
         * it alone.  If some exec bits are set, it's a real sgid; kill it.
         */
        if (unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
                kill |= ATTR_KILL_SGID;

        if (unlikely(kill && !capable(CAP_FSETID) && S_ISREG(mode)))
                return kill;

        return 0;
}

/**
 * dattobd_do_truncate() - Modifies the attributes of the &struct file object
 *                         to indicate a new file size.  For security the SUID
 *                         and/or SGID bits are removed from the @filp object
 *                         as well.
 * @dentry: The &struct dentry describing the @filp parent.
 * @length: The new length, in bytes.
 * @time_attrs: Time attributes.
 * @filp: The &struct file object.
 *
 * Reimplemented from linux kernel since it isn't exported in the vanilla
 * kernel.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
static int dattobd_do_truncate(struct dentry *dentry, loff_t length,
                               unsigned int time_attrs, struct file *filp)
{
        int ret;
        struct iattr newattrs;

        if (length < 0)
                return -EINVAL;

        newattrs.ia_size = length;
        newattrs.ia_valid = ATTR_SIZE | time_attrs;
        if (filp) {
                newattrs.ia_file = filp;
                newattrs.ia_valid |= ATTR_FILE;
        }

        ret = dattobd_should_remove_suid(dentry);
        if (ret)
                newattrs.ia_valid |= ret | ATTR_FORCE;

        dattobd_inode_lock(dentry->d_inode);
#ifdef HAVE_NOTIFY_CHANGE_2
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
        ret = notify_change(dentry, &newattrs);
#elif defined HAVE_USER_NAMESPACE_ARGS
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0)
        ret = notify_change(&init_user_ns, dentry, &newattrs, NULL);
#else
        ret = notify_change(dentry, &newattrs, NULL);
#endif
        dattobd_inode_unlock(dentry->d_inode);

        return ret;
}

/**
 * file_truncate() - Truncates a file to a given length.
 *
 * @filp: A &struct file object.
 * @len: The truncation length in bytes.
 *
 * Special treatment of SUID and SGID is performed.  See @dattobd_do_truncate.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int file_truncate(struct file *filp, loff_t len)
{
        struct inode *inode;
        struct dentry *dentry;
        int ret;

        dentry = dattobd_get_dentry(filp);
        inode = dentry->d_inode;

#ifdef HAVE_SB_START_WRITE
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
        sb_start_write(inode->i_sb);
#endif

        ret = dattobd_do_truncate(dentry, len, ATTR_MTIME | ATTR_CTIME, filp);

#ifdef HAVE_SB_START_WRITE
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
        sb_end_write(inode->i_sb);
#endif

        if (ret) {
                LOG_ERROR(ret, "error performing truncation");
                goto error;
        }

        return 0;

error:
        LOG_ERROR(ret, "error truncating file");
        return ret;
}

#ifdef HAVE_VFS_FALLOCATE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)

/**
 * real_fallocate() - Allows the caller to allocate disk space for a file
 * within the range specified by @offset and @length.  Any subregion within
 * This domain that didn't have data before the call will contain zeroes.
 *
 * @f: A &struct file object.
 * @offset: The offset into @f indicating the start of the allocation.
 * @length: The number of byte to allocate starting at @offset.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
#define real_fallocate(f, offset, length) vfs_fallocate(f, 0, offset, length)

#else

/**
 * real_fallocate() - Allows the caller to allocate disk space for a file
 * within the range specified by @offset and @length.  Any subregion within
 * this domain that didn't have data before the call will contain zeroes.
 *
 * @f: A &struct file object.
 * @offset: The offset into @f indicating the start of the allocation.
 * @length: The number of byte to allocate starting at @offset.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
static int real_fallocate(struct file *f, uint64_t offset, uint64_t length)
{
        int ret;
        loff_t off = offset;
        loff_t len = length;
#ifndef HAVE_FILE_INODE
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
        struct inode *inode = dattobd_get_dentry(f)->d_inode;
#else
        struct inode *inode = file_inode(f);
#endif

        if (off + len > inode->i_sb->s_maxbytes || off + len < 0)
                return -EFBIG;

#if !defined(HAVE_IOPS_FALLOCATE) && !defined(HAVE_FOPS_FALLOCATE)
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
        return -EOPNOTSUPP;
#elif defined(HAVE_IOPS_FALLOCATE)
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
        if (!inode->i_op->fallocate)
                return -EOPNOTSUPP;
        ret = inode->i_op->fallocate(inode, 0, offset, len);
#else
        if (!f->f_op->fallocate)
                return -EOPNOTSUPP;
#ifdef HAVE_SB_START_WRITE
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
        sb_start_write(inode->i_sb);
#endif
        ret = f->f_op->fallocate(f, 0, off, len);
#ifdef HAVE_SB_START_WRITE
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
        sb_end_write(inode->i_sb);
#endif
#endif

        return ret;
}
#endif

/**
 * file_allocate() - Allows the caller to allocate disk space for a file
 * within the range specified by @offset and @length.  Attempts to use
 * @real_fallocate with a fallback of writing zeroes if that fails.
 *
 * @f: A &struct file object.
 * @offset: The offset into @f indicating the start of the allocation.
 * @length: The number of byte to allocate starting at @offset.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int file_allocate(struct file *f, uint64_t offset, uint64_t length)
{
        int ret = 0;
        char *page_buf = NULL;
        uint64_t i, write_count;
        char *abs_path = NULL;
        int abs_path_len;

        file_get_absolute_pathname(f, &abs_path, &abs_path_len);

        // try regular fallocate
        ret = real_fallocate(f, offset, length);
        if (ret && ret != -EOPNOTSUPP)
                goto error;
        else if (!ret)
                goto out;

        // fallocate isn't supported, fall back on writing zeros
        if (!abs_path) {
                LOG_WARN("fallocate is not supported for this file system, "
                         "falling back on "
                         "writing zeros");
        } else {
                LOG_WARN("fallocate is not supported for '%s', falling back on "
                         "writing zeros",
                         abs_path);
        }

        // allocate page of zeros
        page_buf = (char *)get_zeroed_page(GFP_KERNEL);
        if (!page_buf) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating zeroed page");
                goto error;
        }

        // may write up to a page too much, ok for our use case
        write_count = NUM_SEGMENTS(length, PAGE_SHIFT);

        // if not page aligned, write zeros to that point
        if (offset % PAGE_SIZE != 0) {
                ret = file_write(f, page_buf, offset,
                                 PAGE_SIZE - (offset % PAGE_SIZE));
                if (ret)
                        goto error;

                offset += PAGE_SIZE - (offset % PAGE_SIZE);
        }

        // write a page of zeros at a time
        for (i = 0; i < write_count; i++) {
                ret = file_write(f, page_buf, offset + (PAGE_SIZE * i),
                                 PAGE_SIZE);
                if (ret)
                        goto error;
        }

out:
        if (page_buf)
                free_page((unsigned long)page_buf);
        if (abs_path)
                kfree(abs_path);

        return 0;

error:
        if (!abs_path) {
                LOG_ERROR(ret, "error performing fallocate");
        } else {
                LOG_ERROR(ret, "error performing fallocate on file '%s'",
                          abs_path);
        }

        if (page_buf)
                free_page((unsigned long)page_buf);
        if (abs_path)
                kfree(abs_path);

        return ret;
}

/**
 * __file_unlink() - delete a name and possibly the file it refers to.
 * @filp: A &struct file object.
 * @close: Close the @filp on success.
 * @force: Always close the @filp regardless of a successful unlink.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int __file_unlink(struct file *filp, int close, int force)
{
        int ret = 0;
        struct inode *dir_inode = dattobd_get_dentry(filp)->d_parent->d_inode;
        struct dentry *file_dentry = dattobd_get_dentry(filp);
        struct vfsmount *mnt = dattobd_get_mnt(filp);

        if (d_unlinked(file_dentry)) {
                if (close)
                        file_close(filp);
                return 0;
        }

        dget(file_dentry);
        igrab(dir_inode);

        ret = mnt_want_write(mnt);
        if (ret) {
                LOG_ERROR(ret, "error getting write access to vfs mount");
                goto mnt_error;
        }

#ifdef HAVE_VFS_UNLINK_2
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
        ret = vfs_unlink(dir_inode, file_dentry);
#elif defined HAVE_USER_NAMESPACE_ARGS
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0)
        ret = vfs_unlink(&init_user_ns, dir_inode, file_dentry, NULL);
#else
        ret = vfs_unlink(dir_inode, file_dentry, NULL);
#endif
        if (ret) {
                LOG_ERROR(ret, "error unlinking file");
                goto error;
        }

error:
        mnt_drop_write(mnt);

        if (close && (!ret || force))
                file_close(filp);

mnt_error:
        iput(dir_inode);
        dput(file_dentry);

        return ret;
}

#ifndef HAVE_D_UNLINKED
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)

/**
 * d_unlinked() - Checks to see if a directory has been unlinked.
 * @dentry: A &struct dentry object pointer.
 *
 * Return: A boolean indicating whether the dentry has been unlinked.
 */
int d_unlinked(struct dentry *dentry)
{
        return d_unhashed(dentry) && !IS_ROOT(dentry);
}

#endif

#ifndef HAVE_NOOP_LLSEEK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)

/**
 * noop_llseek() - No operation performed llseek implementation
 * @file: The &struct file object to seek on
 * @offset: file offset to seek to
 * @origin: type of seek
 *
 * Reimplemented from linux kernel since it isn't universally available.
 *
 * Return: the current file position.
 */
loff_t noop_llseek(struct file *file, loff_t offset, int origin)
{
        return file->f_pos;
}

#endif

#ifndef HAVE_PATH_PUT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)

/**
 * path_put() - Put a reference to a path.
 * @path: path to put the reference to.
 *
 * Given a path decrement the reference count to the dentry and the vfsmount.
 */
void path_put(const struct path *path)
{
        dput(path->dentry);
        mntput(path->mnt);
}
#endif

#ifndef HAVE_INODE_LOCK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
/**
 * dattobd_inode_lock() - Locks the inode's mutex.
 * @inode: The &struct inode object pointer.
 *
 * Reimplementation from Linux kernel since it isn't universally available.
 */
void dattobd_inode_lock(struct inode *inode)
{
        mutex_lock(&inode->i_mutex);
}

/**
 * dattobd_inode_unlock() - Unlocks the inode's mutex.
 * @inode: The &struct inode object pointer.
 *
 * Reimplementation from Linux kernel since it isn't universally available.
 */
void dattobd_inode_unlock(struct inode *inode)
{
        mutex_unlock(&inode->i_mutex);
}

#endif
