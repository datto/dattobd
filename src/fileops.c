/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include <fileops.h>
#include <universion.h>

void file_close(struct file *f){
	filp_close(f, 0);
}

int file_open(const char *filename, int flags, struct file **filp){
	int ret;
	struct file *f;

	f = filp_open(filename, flags | O_RDWR | O_LARGEFILE, 0);
	if(!f){
		ret = -EFAULT;
		LOG_ERROR(ret, "error creating/opening file '%s' (null pointer)", filename);
		goto file_open_error;
	}else if(IS_ERR(f)){
		ret = PTR_ERR(f);
		f = NULL;
		LOG_ERROR(ret, "error creating/opening file '%s' - %d", filename, (int)PTR_ERR(f));
		goto file_open_error;
	}else if(!S_ISREG(f->f_path.dentry->d_inode->i_mode)){
		ret = -EINVAL;
		LOG_ERROR(ret, "file specified is not a regular file");
		goto file_open_error;
	}
	f->f_mode |= FMODE_NONOTIFY;

	*filp = f;
	return 0;

file_open_error:
	LOG_ERROR(ret, "error opening specified file");
	if(f) file_close(f);

	*filp = NULL;
	return ret;
}

int file_io(struct file *filp, int is_write, void *buf, sector_t offset, unsigned long len){
	ssize_t ret;
	mm_segment_t old_fs;
	loff_t off = (loff_t)offset;

	//change context for file write
	old_fs = get_fs();
	set_fs(get_ds());

	//perform the read or write
	if(is_write) ret = vfs_write(filp, buf, len, &off);
	else ret = vfs_read(filp, buf, len, &off);

	//revert context
	set_fs(old_fs);

	if(ret < 0){
		LOG_ERROR((int)ret, "error performing file '%s': %llu, %lu", (is_write)? "write" : "read", (unsigned long long)offset, len);
		return ret;
	}else if(ret != len){
		LOG_ERROR(-EIO, "invalid file '%s' size: %llu, %lu, %lu", (is_write)? "write" : "read", (unsigned long long)offset, len, (unsigned long)ret);
		ret = -EIO;
		return ret;
	}

	return 0;
}

//reimplemented from linux kernel (it isn't exported in the vanilla kernel)
static int dattobd_do_truncate(struct dentry *dentry, loff_t length, unsigned int time_attrs, struct file *filp){
	int ret;
	struct iattr newattrs;

	if(length < 0) return -EINVAL;

	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | time_attrs;
	if(filp) {
		newattrs.ia_file = filp;
		newattrs.ia_valid |= ATTR_FILE;
	}

	ret = should_remove_suid(dentry);
	if(ret) newattrs.ia_valid |= ret | ATTR_FORCE;

	mutex_lock(&dentry->d_inode->i_mutex);
#ifdef HAVE_NOTIFY_CHANGE_2
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
	ret = notify_change(dentry, &newattrs);
#else
	ret = notify_change(dentry, &newattrs, NULL);
#endif
	mutex_unlock(&dentry->d_inode->i_mutex);

	return ret;
}

int file_truncate(struct file *filp, loff_t len){
	struct inode *inode;
	struct dentry *dentry;
	int ret;

	dentry = filp->f_path.dentry;
	inode = dentry->d_inode;

	ret = locks_verify_truncate(inode, filp, len);
	if(ret){
		LOG_ERROR(ret, "error verifying truncation is possible");
		goto file_truncate_error;
	}

#ifdef HAVE_SB_START_WRITE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	sb_start_write(inode->i_sb);
#endif

	ret = dattobd_do_truncate(dentry, len, ATTR_MTIME|ATTR_CTIME, filp);

#ifdef HAVE_SB_START_WRITE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	sb_end_write(inode->i_sb);
#endif

	if(ret){
		LOG_ERROR(ret, "error performing truncation");
		goto file_truncate_error;
	}

	return 0;

file_truncate_error:
	LOG_ERROR(ret, "error truncating file");
	return ret;
}

#ifndef HAVE_VFS_FALLOCATE
int real_fallocate(struct file *f, uint64_t offset, uint64_t length){
	int ret;
	loff_t off = offset;
	loff_t len = length;
	#ifndef HAVE_FILE_INODE
	//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	struct inode *inode = f->f_path.dentry->d_inode;
	#else
	struct inode *inode = file_inode(f);
	#endif

	if(off + len > inode->i_sb->s_maxbytes || off + len < 0) return -EFBIG;

	#ifdef HAVE_IOPS_FALLOCATE
	//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
	if(!inode->i_op->fallocate) return -EOPNOTSUPP;
	ret = inode->i_op->fallocate(inode, 0, offset, len);
	#else

	if(!f->f_op->fallocate) return -EOPNOTSUPP;
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

int file_allocate(struct file *f, uint64_t offset, uint64_t length){
	int ret = 0;
	char *page_buf;
	uint64_t i, write_count;

	//try regular fallocate
	ret = real_fallocate(f, offset, length);
	if(ret && ret != -EOPNOTSUPP){
		LOG_ERROR(ret, "error performing real fallocate");
		return ret;
	}else if(!ret) return 0;

	//fallocate isn't supported, fall back on writing zeros
	LOG_WARN("fallocate is not supported for this file system, falling back on writing zeros");

	//allocate page of zeros
	page_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if(!page_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating zeroed page");
		goto file_allocate_error;
	}

	//may write up to a page too much, ok for our use case
	write_count = NUM_SEGMENTS(length, PAGE_SHIFT);

	//if not page aligned, write zeros to that point
	if(offset % PAGE_SIZE != 0){
		ret = file_write(f, page_buf, offset, PAGE_SIZE - (offset % PAGE_SIZE));
		if(ret)	goto file_allocate_error;

		offset += PAGE_SIZE - (offset % PAGE_SIZE);
	}

	//write a page of zeros at a time
	for(i = 0; i < write_count; i++){
		ret = file_write(f, page_buf, offset + (PAGE_SIZE * i), PAGE_SIZE);
		if(ret) goto file_allocate_error;
	}

	free_page((unsigned long)page_buf);
	return 0;

file_allocate_error:
	LOG_ERROR(ret, "error performing fallocate");
	if(page_buf) free_page((unsigned long)page_buf);
	return ret;
}

int __file_unlink(struct file *filp, int close, int force){
	int ret = 0;
	struct inode *dir_inode = filp->f_path.dentry->d_parent->d_inode;
	struct dentry *file_dentry = filp->f_path.dentry;
	struct vfsmount *mnt = filp->f_path.mnt;

	if(d_unlinked(file_dentry)){
		if(close) file_close(filp);
		return 0;
	}

	dget(file_dentry);
	igrab(dir_inode);

	ret = mnt_want_write(mnt);
	if(ret){
		LOG_ERROR(ret, "error getting write access to vfs mount");
		goto file_unlink_mnt_error;
	}
	mutex_lock(&dir_inode->i_mutex);

#ifdef HAVE_VFS_UNLINK_2
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
	ret = vfs_unlink(dir_inode, file_dentry);
#else
	ret = vfs_unlink(dir_inode, file_dentry, NULL);
#endif
	if(ret){
		LOG_ERROR(ret, "error unlinking file");
		goto file_unlink_error;
	}

file_unlink_error:
	mutex_unlock(&dir_inode->i_mutex);
	mnt_drop_write(mnt);

	if(close && (!ret || force)) file_close(filp);

file_unlink_mnt_error:
	iput(dir_inode);
	dput(file_dentry);

	return ret;
}

#if !defined(HAVE___DENTRY_PATH) && !defined(HAVE_DENTRY_PATH_RAW)
int dentry_get_relative_pathname(struct dentry *dentry, char **buf, int *len_res){
	int len = 0;
	char *pathname;
	struct dentry *parent = dentry;

	while(parent->d_parent != parent){
		len += parent->d_name.len + 1;
		parent = parent->d_parent;
	}

	pathname = kmalloc(len + 1, GFP_KERNEL);
	if(!pathname){
		LOG_ERROR(-ENOMEM, "error allocating pathname for dentry");
		return -ENOMEM;
	}
	pathname[len] = '\0';
	if(len_res) *len_res = len;
	*buf = pathname;

	parent = dentry;
	while(parent->d_parent != parent){
		len -= parent->d_name.len + 1;
		pathname[len] = '/';
		strncpy(&pathname[len + 1], parent->d_name.name, parent->d_name.len);
		parent = parent->d_parent;
	}

	return 0;
}
#else
int dentry_get_relative_pathname(struct dentry *dentry, char **buf, int *len_res){
	int ret, len;
	char *pathname, *page_buf, *final_buf = NULL;

	page_buf = (char *)__get_free_page(GFP_KERNEL);
	if(!page_buf){
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
	if(IS_ERR(pathname)){
		ret = PTR_ERR(pathname);
		pathname = NULL;
		LOG_ERROR(ret, "error fetching dentry pathname");
		goto dentry_get_relative_pathname_error;
	}

	len = page_buf + PAGE_SIZE - pathname;
	final_buf = kmalloc(len, GFP_KERNEL);
	if(!final_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating pathname for dentry");
		goto dentry_get_relative_pathname_error;
	}

	strncpy(final_buf, pathname, len);
	free_page((unsigned long)page_buf);

	*buf = final_buf;
	if(len_res) *len_res = len;
	return 0;

dentry_get_relative_pathname_error:
	LOG_ERROR(ret, "error converting dentry to relative path name");
	if(final_buf) kfree(final_buf);
	if(page_buf) free_page((unsigned long)page_buf);

	*buf = NULL;
	if(len_res) *len_res = 0;
	return ret;
}
#endif

int path_get_absolute_pathname(struct path *path, char **buf, int *len_res){
	int ret, len;
	char *pathname, *page_buf, *final_buf = NULL;

	page_buf = (char *)__get_free_page(GFP_KERNEL);
	if(!page_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating page for absolute pathname");
		goto path_get_absolute_pathname_error;
	}

	pathname = d_path(path, page_buf, PAGE_SIZE);
	if(IS_ERR(pathname)){
		ret = PTR_ERR(pathname);
		pathname = NULL;
		LOG_ERROR(ret, "error fetching absolute pathname");
		goto path_get_absolute_pathname_error;
	}

	len = page_buf + PAGE_SIZE - pathname;
	final_buf = kmalloc(len, GFP_KERNEL);
	if(!final_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating buffer for absolute pathname");
		goto path_get_absolute_pathname_error;
	}

	strncpy(final_buf, pathname, len);
	free_page((unsigned long)page_buf);

	*buf = final_buf;
	if(len_res) *len_res = len;
	return 0;

path_get_absolute_pathname_error:
	LOG_ERROR(ret, "error getting absolute pathname from path");
	if(final_buf) kfree(final_buf);
	if(page_buf) free_page((unsigned long)page_buf);

	*buf = NULL;
	if(len_res) *len_res = 0;
	return ret;
}

int pathname_to_absolute(char *pathname, char **buf, int *len_res){
	int ret;
	struct path path = {};

	ret = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if(ret){
		LOG_ERROR(ret, "error finding path for pathname");
		return ret;
	}

	ret = path_get_absolute_pathname(&path, buf, len_res);
	if(ret) goto pathname_to_absolute_error;

	path_put(&path);
	return 0;

pathname_to_absolute_error:
	LOG_ERROR(ret, "error converting pathname to absolute pathname");
	path_put(&path);
	return ret;
}

int pathname_concat(char *pathname1, char *pathname2, char **path_out){
	int pathname1_len, pathname2_len, need_leading_slash = 0;
	char *full_pathname;

	pathname1_len = strlen(pathname1);
	pathname2_len = strlen(pathname2);

	if(pathname1[pathname1_len - 1] != '/' && pathname2[0] != '/') need_leading_slash = 1;
	else if(pathname1[pathname1_len - 1] == '/' && pathname2[0] == '/') pathname1_len--;

	full_pathname = kmalloc(pathname1_len + pathname2_len + need_leading_slash + 1, GFP_KERNEL);
	if(!full_pathname){
		LOG_ERROR(-ENOMEM, "error allocating buffer for pathname concatenation");
		*path_out = NULL;
		return -ENOMEM;
	}
	full_pathname[pathname1_len + need_leading_slash + pathname2_len] = '\0';

	strncpy(full_pathname, pathname1, pathname1_len);
	if(need_leading_slash) full_pathname[pathname1_len] = '/';
	strncpy(full_pathname + pathname1_len + need_leading_slash, pathname2, pathname2_len);

	*path_out = full_pathname;
	return 0;
}

int user_mount_pathname_concat(char __user *user_mount_path, char *rel_path, char **path_out){
	int ret;
	char *mount_path;

	ret = copy_string_from_user(user_mount_path, &mount_path);
	if(ret) goto user_mount_pathname_concat_error;

	ret = pathname_concat(mount_path, rel_path, path_out);
	if(ret) goto user_mount_pathname_concat_error;

	kfree(mount_path);
	return 0;

user_mount_pathname_concat_error:
	LOG_ERROR(ret, "error concatenating mount path to relative path");
	if(mount_path) kfree(mount_path);

	*path_out = NULL;
	return ret;
}