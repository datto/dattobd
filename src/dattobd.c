// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"
#include "kernel-config.h"
#include "dattobd.h"

//current lowest supported kernel = 2.6.18

//basic information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tom Caputi");
MODULE_DESCRIPTION("Kernel module for supporting block device snapshots and incremental backups.");
MODULE_VERSION(DATTOBD_VERSION);

//printing macros
#define LOG_DEBUG(fmt, args...) \
	do{ \
		if(dattobd_debug) printk(KERN_DEBUG "datto: " fmt "\n", ## args); \
	}while(0)

#define LOG_WARN(fmt, args...) printk(KERN_WARNING "datto: " fmt "\n", ## args)
#define LOG_ERROR(error, fmt, args...) printk(KERN_ERR "datto: " fmt ": %d\n", ## args, error)
#define PRINT_BIO(text, bio) LOG_DEBUG(text ": sect = %llu size = %u", (unsigned long long)bio_sector(bio), bio_size(bio) / 512)

/*********************************REDEFINED FUNCTIONS*******************************/

#ifdef HAVE_UUID_H
#include <linux/uuid.h>
#endif

#ifdef HAVE_UAPI_MOUNT_H
#include <uapi/linux/mount.h>
#endif

#ifndef HAVE_BIO_LIST
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
struct bio_list {
	struct bio *head;
	struct bio *tail;
};

#define BIO_EMPTY_LIST { NULL, NULL }
#define bio_list_for_each(bio, bl) for((bio) = (bl)->head; (bio); (bio) = (bio)->bi_next)

static inline int bio_list_empty(const struct bio_list *bl){
	return bl->head == NULL;
}

static inline void bio_list_init(struct bio_list *bl){
	bl->head = bl->tail = NULL;
}

static inline void bio_list_add(struct bio_list *bl, struct bio *bio){
	bio->bi_next = NULL;

	if (bl->tail) bl->tail->bi_next = bio;
	else bl->head = bio;

	bl->tail = bio;
}

static inline struct bio *bio_list_pop(struct bio_list *bl){
	struct bio *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head) bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}
#endif

#ifndef HAVE_D_UNLINKED
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
static inline int d_unlinked(struct dentry *dentry){
	return d_unhashed(dentry) && !IS_ROOT(dentry);
}
#endif

#ifndef HAVE_NOOP_LLSEEK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static loff_t noop_llseek(struct file *file, loff_t offset, int origin){
	return file->f_pos;
}
#endif


#ifndef HAVE_STRUCT_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
struct path {
	struct vfsmount *mnt;
	struct dentry *dentry;
};
#define dattobd_get_dentry(f) (f)->f_dentry
#define dattobd_get_mnt(f) (f)->f_vfsmnt
#else
#define dattobd_get_dentry(f) (f)->f_path.dentry
#define dattobd_get_mnt(f) (f)->f_path.mnt
#endif

#ifndef HAVE_PATH_PUT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
void path_put(const struct path *path) {
	dput(path->dentry);
	mntput(path->mnt);
}
#define dattobd_d_path(path, page_buf, page_size) d_path((path)->dentry, (path)->mnt, page_buf, page_size)
#define dattobd_get_nd_dentry(nd) (nd).dentry
#define dattobd_get_nd_mnt(nd) (nd).mnt
#else
#define dattobd_d_path(path, page_buf, page_size) d_path(path, page_buf, page_size)
#define dattobd_get_nd_dentry(nd) (nd).path.dentry
#define dattobd_get_nd_mnt(nd) (nd).path.mnt
#endif

#ifndef HAVE_FMODE_T
typedef mode_t fmode_t;
#endif

#ifdef HAVE_BD_SUPER
#define dattobd_get_super(bdev) (bdev)->bd_super
#define dattobd_drop_super(sb)
#else
#define dattobd_get_super(bdev) get_super(bdev)
#define dattobd_drop_super(sb) drop_super(sb)
#endif

#ifndef HAVE_BLKDEV_GET_BY_PATH
struct block_device *dattobd_lookup_bdev(const char *pathname, fmode_t mode) {
	int r;
	struct block_device *retbd;
	struct nameidata nd;
	struct inode *inode;
	dev_t dev;

	if ((r = path_lookup(pathname, LOOKUP_FOLLOW, &nd)))
		goto fail;

	inode = dattobd_get_nd_dentry(nd)->d_inode;
	if (!inode) {
		r = -ENOENT;
		goto fail;
	}

	if (!S_ISBLK(inode->i_mode)) {
		r = -ENOTBLK;
		goto fail;
	}
	dev = inode->i_rdev;
	retbd = open_by_devnum(dev, mode);

out:
#ifdef HAVE_PATH_PUT
	path_put(&nd.path);
#else
	dput(nd.dentry);
	mntput(nd.mnt);
#endif
	return retbd;
fail:
	retbd = ERR_PTR(r);
	goto out;
}
#endif

#ifndef HAVE_BLKDEV_GET_BY_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
static struct block_device *blkdev_get_by_path(const char *path, fmode_t mode, void *holder){
	struct block_device *bdev;
	bdev = dattobd_lookup_bdev(path, mode);
	if(IS_ERR(bdev))
		return bdev;

	if((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
#ifdef HAVE_BLKDEV_PUT_1
		blkdev_put(bdev);
#else
		blkdev_put(bdev, mode);
#endif
		return ERR_PTR(-EACCES);
	}

	return bdev;
}
#endif

#ifndef READ_SYNC
#define READ_SYNC 0
#endif

#ifndef REQ_WRITE
#define REQ_WRITE WRITE
#endif

#ifndef REQ_FLUSH
#define REQ_FLUSH (1 << BIO_RW_BARRIER)
#endif

//if these don't exist they are not supported
#ifndef REQ_SECURE
#define REQ_SECURE 0
#endif

#ifndef REQ_WRITE_SAME
#define REQ_WRITE_SAME 0
#endif

#ifndef HAVE_SUBMIT_BIO_1

#ifndef REQ_DISCARD
#define REQ_DISCARD 0
#endif

#ifndef HAVE_ENUM_REQ_OP
typedef enum req_op {
	REQ_OP_READ,
	REQ_OP_WRITE,
	REQ_OP_DISCARD,         /* request to discard sectors */
	REQ_OP_SECURE_ERASE,    /* request to securely erase sectors */
	REQ_OP_WRITE_SAME,      /* write same block many times */
	REQ_OP_FLUSH,           /* request for cache flush */
} req_op_t;

static inline void dattobd_set_bio_ops(struct bio *bio, req_op_t op, unsigned op_flags){
	bio->bi_rw = 0;

	switch(op){
	case REQ_OP_READ:
		break;
	case REQ_OP_WRITE:
		bio->bi_rw |= REQ_WRITE;
		break;
	case REQ_OP_DISCARD:
		bio->bi_rw |= REQ_DISCARD;
		break;
	case REQ_OP_SECURE_ERASE:
		bio->bi_rw |= REQ_DISCARD | REQ_SECURE;
		break;
	case REQ_OP_WRITE_SAME:
		bio->bi_rw |= REQ_WRITE_SAME;
		break;
	case REQ_OP_FLUSH:
		bio->bi_rw |= REQ_FLUSH;
		break;
	}

	bio->bi_rw |= op_flags;
}
#else
	typedef enum req_op req_op_t;
	#define dattobd_set_bio_ops(bio, op, flags) bio_set_op_attrs(bio, op, flags)
#endif

	#define bio_is_discard(bio) ((bio)->bi_rw & REQ_DISCARD)
	#define dattobd_submit_bio(bio) submit_bio(0, bio)
	#define dattobd_submit_bio_wait(bio) submit_bio_wait(0, bio)

static inline int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag){
	return bio->bi_rw & flag;
}

static inline void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag){
	bio->bi_rw |= flag;
}

static inline void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag){
	bio->bi_rw &= ~flag;
}
#else

#ifndef HAVE_ENUM_REQ_OPF
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
typedef enum req_op req_op_t;
#else
typedef enum req_opf req_op_t;
#endif

static inline void dattobd_set_bio_ops(struct bio *bio, req_op_t op, unsigned op_flags){
	bio->bi_opf = 0;
	bio_set_op_attrs(bio, op, op_flags);
}

static inline int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag){
	return bio->bi_opf & flag;
}

static inline void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag){
	bio->bi_opf |= flag;
}

static inline void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag){
	bio->bi_opf &= ~flag;
}

	#ifdef REQ_DISCARD
		#define bio_is_discard(bio) ((bio)->bi_opf & REQ_DISCARD)
	#else
		#define bio_is_discard(bio) (bio_op(bio) == REQ_OP_DISCARD || bio_op(bio) == REQ_OP_SECURE_ERASE)
	#endif
	#define dattobd_submit_bio(bio) submit_bio(bio)
	#define dattobd_submit_bio_wait(bio) submit_bio_wait(bio)
#endif

#if !defined HAVE_SUBMIT_BIO_WAIT && !defined HAVE_SUBMIT_BIO_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
struct submit_bio_ret{
	struct completion event;
	int error;
};

static void __submit_bio_wait_endio(struct bio *bio, int error){
	struct submit_bio_ret *ret = bio->bi_private;
	ret->error = error;
	complete(&ret->event);
}

#ifdef HAVE_BIO_ENDIO_INT
static int submit_bio_wait_endio(struct bio *bio, unsigned int bytes, int error){
	if (bio->bi_size) return 1;

	__submit_bio_wait_endio(bio, error);
	return 0;
}
#else
static void submit_bio_wait_endio(struct bio *bio, int error){
	__submit_bio_wait_endio(bio, error);
}
#endif

static int submit_bio_wait(int rw, struct bio *bio){
	struct submit_bio_ret ret;

	//kernel implementation has the line below, but all our calls will have this already and it changes across kernel versions
	//rw |= REQ_SYNC;

	init_completion(&ret.event);
	bio->bi_private = &ret;
	bio->bi_end_io = submit_bio_wait_endio;
	submit_bio(rw, bio);
	wait_for_completion(&ret.event);

	return ret.error;
}
#endif

#ifdef HAVE_BIO_ENDIO_INT
static void dattobd_bio_endio(struct bio *bio, int err){
	bio_endio(bio, bio->bi_size, err);
}
#elif !defined HAVE_BIO_ENDIO_1
static void dattobd_bio_endio(struct bio *bio, int err){
	bio_endio(bio, err);
}
#elif defined HAVE_BLK_STATUS_T
static void dattobd_bio_endio(struct bio *bio, int err){
	bio->bi_status = errno_to_blk_status(err);
	bio_endio(bio);
}
#else
static void dattobd_bio_endio(struct bio *bio, int err){
	bio->bi_error = err;
	bio_endio(bio);
}
#endif

//the kernel changed the usage of bio_for_each_segment in 3.14. Do not use any fields directly or you will lose compatibility.
#ifndef HAVE_BVEC_ITER
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	typedef int bio_iter_t;
	typedef struct bio_vec *bio_iter_bvec_t;
	#define bio_iter_len(bio, iter) ((bio)->bi_io_vec[(iter)].bv_len)
	#define bio_iter_offset(bio, iter) ((bio)->bi_io_vec[(iter)].bv_offset)
	#define bio_iter_page(bio, iter) ((bio)->bi_io_vec[(iter)].bv_page)
	#define bio_iter_idx(iter) (iter)
	#define bio_sector(bio) (bio)->bi_sector
	#define bio_size(bio) (bio)->bi_size
	#define bio_idx(bio) (bio)->bi_idx
#else
	typedef struct bvec_iter bio_iter_t;
	typedef struct bio_vec bio_iter_bvec_t;
	#define bio_iter_idx(iter) ((iter).bi_idx)
	#define bio_sector(bio) (bio)->bi_iter.bi_sector
	#define bio_size(bio) (bio)->bi_iter.bi_size
	#define bio_idx(bio) (bio)->bi_iter.bi_idx
#endif


#ifndef HAVE_MNT_WANT_WRITE
#define mnt_want_write(x) 0
#define mnt_drop_write (void)sizeof
#endif

#ifndef UMOUNT_NOFOLLOW
#define UMOUNT_NOFOLLOW 0
#endif

#if !defined(HAVE_BDEV_STACK_LIMITS) && !defined(HAVE_BLK_SET_DEFAULT_LIMITS)
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)

#ifndef min_not_zero
#define min_not_zero(l, r) ((l) == 0) ? (r) : (((r) == 0) ? (l) : min(l, r))
#endif

int blk_stack_limits(struct request_queue *t, struct request_queue *b, sector_t offset){
	t->max_sectors = min_not_zero(t->max_sectors, b->max_sectors);
	t->max_hw_sectors = min_not_zero(t->max_hw_sectors, b->max_hw_sectors);
	t->bounce_pfn = min_not_zero(t->bounce_pfn, b->bounce_pfn);
	t->seg_boundary_mask = min_not_zero(t->seg_boundary_mask, b->seg_boundary_mask);
	t->max_phys_segments = min_not_zero(t->max_phys_segments, b->max_phys_segments);
	t->max_hw_segments = min_not_zero(t->max_hw_segments, b->max_hw_segments);
	t->max_segment_size = min_not_zero(t->max_segment_size, b->max_segment_size);
	return 0;
}

int dattobd_bdev_stack_limits(struct request_queue *t, struct block_device *bdev, sector_t start){
	struct request_queue *bq = bdev_get_queue(bdev);
	start += get_start_sect(bdev);
	return blk_stack_limits(t, bq, start << 9);
}

#elif !defined(HAVE_BDEV_STACK_LIMITS)
//#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)

int bdev_stack_limits(struct queue_limits *t, struct block_device *bdev, sector_t start){
	struct request_queue *bq = bdev_get_queue(bdev);
	start += get_start_sect(bdev);
	return blk_stack_limits(t, &bq->limits, start << 9);
}
#define dattobd_bdev_stack_limits(queue, bdev, start) bdev_stack_limits(&(queue)->limits, bdev, start)

#else
#define dattobd_bdev_stack_limits(queue, bdev, start) bdev_stack_limits(&(queue)->limits, bdev, start)
#endif

#ifndef HAVE_KERN_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int kern_path(const char *name, unsigned int flags, struct path *path){
	struct nameidata nd;
	int ret = path_lookup(name, flags, &nd);
	if(!ret){
		path->dentry = dattobd_get_nd_dentry(nd);
		path->mnt = dattobd_get_nd_mnt(nd);
	}
	return ret;
}
#endif

#ifndef HAVE_BLK_SET_DEFAULT_LIMITS
#define blk_set_default_limits(ql)
#endif

#ifdef HAVE_BIOSET_NEED_BVECS_FLAG
#define dattobd_bioset_create(bio_size, bvec_size, scale) bioset_create(bio_size, bvec_size, BIOSET_NEED_BVECS)
#elif defined HAVE_BIOSET_CREATE_3
#define dattobd_bioset_create(bio_size, bvec_size, scale) bioset_create(bio_size, bvec_size, scale)
#else
#define dattobd_bioset_create(bio_size, bvec_size, scale) bioset_create(bio_size, scale)
#endif

#ifndef HAVE_USER_PATH_AT
int user_path_at(int dfd, const char __user *name, unsigned flags, struct path *path) {
	struct nameidata nd;
	char *tmp = getname(name);
	int err = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		BUG_ON(flags & LOOKUP_PARENT);
		err = path_lookup(tmp, flags, &nd);
		putname(tmp);
		if (!err) {
			path->dentry = dattobd_get_nd_dentry(nd);
			path->mnt = dattobd_get_nd_mnt(nd);
		}
	}
	return err;
}
#endif

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


#ifdef HAVE_BLKDEV_PUT_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	#define dattobd_blkdev_put(bdev) blkdev_put(bdev);
#else
	#define dattobd_blkdev_put(bdev) blkdev_put(bdev, FMODE_READ);
#endif

#ifndef HAVE_PART_NR_SECTS_READ
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
	#define dattobd_bdev_size(bdev) ((bdev)->bd_part->nr_sects)
#else
	#define dattobd_bdev_size(bdev) part_nr_sects_read((bdev)->bd_part)
#endif

#ifndef HAVE_VZALLOC
	#define vzalloc(size) __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL)
#endif

#ifdef HAVE_MAKE_REQUEST_FN_INT
	#define MRF_RETURN_TYPE int
	#define MRF_RETURN(ret) return ret

static inline int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio){
	return fn(q, bio);
}
#elif defined HAVE_MAKE_REQUEST_FN_VOID
	#define MRF_RETURN_TYPE void
	#define MRF_RETURN(ret) return

static inline int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio){
	fn(q, bio);
	return 0;
}
#else
	#define MRF_RETURN_TYPE blk_qc_t
	#define MRF_RETURN(ret) return BLK_QC_T_NONE

static inline int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio){
	return fn(q, bio);
}
#endif

#ifndef ACCESS_ONCE
	#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
#endif

//this is defined in 3.16 and up
#ifndef MIN_NICE
	#define MIN_NICE -20
#endif

//if this isn't defined, we don't need it anyway
#ifndef FMODE_NONOTIFY
	#define FMODE_NONOTIFY 0
#endif

#ifndef HAVE_BLK_SET_STACKING_LIMITS
	#define blk_set_stacking_limits(ql) blk_set_default_limits(ql)
#endif

#ifndef HAVE_INODE_LOCK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
static inline void dattobd_inode_lock(struct inode *inode){
	mutex_lock(&inode->i_mutex);
}

static inline void dattobd_inode_unlock(struct inode *inode){
	mutex_unlock(&inode->i_mutex);
}
#else
	#define dattobd_inode_lock inode_lock
	#define dattobd_inode_unlock inode_unlock
#endif

#ifndef HAVE_PROC_CREATE
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
static inline struct proc_dir_entry *proc_create(const char *name, mode_t mode, struct proc_dir_entry *parent, const struct file_operations *proc_fops){
	struct proc_dir_entry *ent;

	ent = create_proc_entry(name, mode, parent);
	if(!ent) goto error;

	ent->proc_fops = proc_fops;

	return ent;

error:
	return NULL;
}
#endif

static inline ssize_t dattobd_kernel_read(struct file *filp, void *buf, size_t count, loff_t *pos){
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

static inline ssize_t dattobd_kernel_write(struct file *filp, const void *buf, size_t count, loff_t *pos){
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

static inline struct request_queue *dattobd_bio_get_queue(struct bio *bio){
#ifdef HAVE_BIO_BI_BDEV
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	return bdev_get_queue(bio->bi_bdev);
#else
	return bio->bi_disk->queue;
#endif
}

static inline void dattobd_bio_set_dev(struct bio *bio, struct block_device *bdev){
#ifdef HAVE_BIO_BI_BDEV
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	bio->bi_bdev = bdev;
#else
	bio_set_dev(bio, bdev);
#endif
}

static inline void dattobd_bio_copy_dev(struct bio *dst, struct bio *src){
#ifdef HAVE_BIO_BI_BDEV
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
	dst->bi_bdev = src->bi_bdev;
#else
	bio_copy_dev(dst, src);
#endif
}

#ifndef HAVE_BIOSET_INIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
#define dev_bioset(dev) ((dev)->sd_bioset)
#else
#define dev_bioset(dev) (&(dev)->sd_bioset)
#endif

/*********************************MACRO/PARAMETER DEFINITIONS*******************************/


//memory macros
#define get_zeroed_pages(flags, order) __get_free_pages(((flags) | __GFP_ZERO), order)

//takes a value and the log of the value it should be rounded up to
#define NUM_SEGMENTS(x, log_size) (((x) + (1<<(log_size)) - 1) >> (log_size))
#define ROUND_UP(x, chunk) ((((x) + (chunk) - 1) / (chunk)) * (chunk))
#define ROUND_DOWN(x, chunk) (((x) / (chunk)) * (chunk))

//bitmap macros
#define bitmap_is_marked(bitmap, pos) (((bitmap)[(pos) / 8] & (1 << ((pos) % 8))) != 0)
#define bitmap_mark(bitmap, pos) (bitmap)[(pos) / 8] |= (1 << ((pos) % 8))

//name macros
#define INFO_PROC_FILE "datto-info"
#define DRIVER_NAME "datto"
#define CONTROL_DEVICE_NAME "datto-ctl"
#define SNAP_DEVICE_NAME "datto%d"
#define SNAP_COW_THREAD_NAME_FMT "datto_snap_cow%d"
#define SNAP_MRF_THREAD_NAME_FMT "datto_snap_mrf%d"
#define INC_THREAD_NAME_FMT "datto_inc%d"

//macro for iterating over snap_devices (requires a null check on dev)
#define tracer_for_each(dev, i) for(i = ACCESS_ONCE(lowest_minor), dev = ACCESS_ONCE(snap_devices[i]); i <= ACCESS_ONCE(highest_minor); i++, dev = ACCESS_ONCE(snap_devices[i]))
#define tracer_for_each_full(dev, i) for(i = 0, dev = ACCESS_ONCE(snap_devices[i]); i < dattobd_max_snap_devices; i++, dev = ACCESS_ONCE(snap_devices[i]))

//returns true if tracing struct's base device queue matches that of bio
#define tracer_queue_matches_bio(dev, bio) (bdev_get_queue((dev)->sd_base_dev) == dattobd_bio_get_queue(bio))

//returns true if tracing struct's sector range matches the sector of the bio
#define tracer_sector_matches_bio(dev, bio) (bio_sector(bio) >= (dev)->sd_sect_off && bio_sector(bio) < (dev)->sd_sect_off + (dev)->sd_size)

//should be called along with *_queue_matches_bio to be valid. returns true if bio is a write, has a size,
//tracing struct is in non-fail state, and the device's sector range matches the bio
#define tracer_should_trace_bio(dev, bio) (bio_data_dir(bio) && !bio_is_discard(bio) && bio_size(bio) && !tracer_read_fail_state(dev) && tracer_sector_matches_bio(dev, bio))

//macros for snapshot bio modes of operation
#define READ_MODE_COW_FILE 1
#define READ_MODE_BASE_DEVICE 2
#define READ_MODE_MIXED 3

//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

//macros for defining sector and block sizes
#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)
#define COW_SECTION_SIZE 4096
#define SECTORS_PER_BLOCK (COW_BLOCK_SIZE / SECTOR_SIZE)
#define SECTOR_TO_BLOCK(sect) ((sect) / SECTORS_PER_BLOCK)
#define BLOCK_TO_SECTOR(block) ((block) * SECTORS_PER_BLOCK)

//maximum number of clones per traced bio
#define MAX_CLONES_PER_BIO 10

//macros for compilation
#define MAYBE_UNUSED(x) (void)(x)

//macros for defining the state of a tracing struct (bit offsets)
#define SNAPSHOT 0
#define ACTIVE 1
#define UNVERIFIED 2

//macros for working with bios
#define BIO_SET_SIZE 256
#define bio_last_sector(bio) (bio_sector(bio) + (bio_size(bio) / SECTOR_SIZE))

// As of Linux 5.2, __REQ_NR_BITS == 26
#define __DATTOBD_PASSTHROUGH 30    /* don't perform COW operation */
#define DATTOBD_PASSTHROUGH (1ULL << __DATTOBD_PASSTHROUGH)

#define DATTOBD_DEFAULT_SNAP_DEVICES 24
#define DATTOBD_MAX_SNAP_DEVICES 255

//global module parameters
static int dattobd_may_hook_syscalls = 1;
static unsigned long dattobd_cow_max_memory_default = (300 * 1024 * 1024);
static unsigned int dattobd_cow_fallocate_percentage_default = 10;
static unsigned int dattobd_max_snap_devices = DATTOBD_DEFAULT_SNAP_DEVICES;
static int dattobd_debug = 0;

module_param_named(may_hook_syscalls, dattobd_may_hook_syscalls, int, S_IRUGO);
MODULE_PARM_DESC(may_hook_syscalls, "if true, allows the kernel module to find and alter the system call table to allow tracing to work across remounts");

module_param_named(cow_max_memory_default, dattobd_cow_max_memory_default, ulong, 0);
MODULE_PARM_DESC(cow_max_memory_default, "default maximum cache size (in bytes)");

module_param_named(cow_fallocate_percentage_default, dattobd_cow_fallocate_percentage_default, uint, 0);
MODULE_PARM_DESC(cow_fallocate_percentage_default, "default space allocated to the cow file (as integer percentage)");

module_param_named(max_snap_devices, dattobd_max_snap_devices, uint, S_IRUGO);
MODULE_PARM_DESC(max_snap_devices, "maximum number of tracers available");

module_param_named(debug, dattobd_debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "enables debug logging");

/*********************************STRUCT DEFINITIONS*******************************/

struct sector_set{
	struct sector_set *next;
	sector_t sect;
	unsigned int len;
};

struct sset_list{
	struct sector_set *head;
	struct sector_set *tail;
};

struct bio_queue{
	struct bio_list bios;
	spinlock_t lock;
	wait_queue_head_t event;
};

struct sset_queue{
	struct sset_list ssets;
	spinlock_t lock;
	wait_queue_head_t event;
};

struct bio_sector_map{
	struct bio *bio;
	sector_t sect;
	unsigned int size;
};

struct tracing_params{
	struct bio *orig_bio;
	struct snap_device *dev;
	atomic_t refs;
	struct bio_sector_map bio_sects[MAX_CLONES_PER_BIO];
};

struct cow_section{
	char has_data; //zero if this section has mappings (on file or in memory)
	unsigned long usage; //counter that keeps track of how often this section is used
	uint64_t *mappings; //array of block addresses
};

struct cow_manager{
	struct file *filp; //the file the cow manager is writing to
	uint32_t flags; //flags representing current state of cow manager
	uint64_t curr_pos; //current write head position
	uint64_t data_offset; //starting offset of data
	uint64_t file_max; //max size of the file before an error is thrown
	uint64_t seqid; //sequence id, increments on each transition to snapshot mode
	uint64_t version; //version of cow file format
	uint64_t nr_changed_blocks; //number of changed blocks since last snapshot
	uint8_t uuid[COW_UUID_SIZE]; //uuid for this series of snaphots
	unsigned int log_sect_pages; //log2 of the number of pages needed to store a section
	unsigned long sect_size; //size of a section in number of elements it can contain
	unsigned long allocated_sects; //number of currently allocated sections
	unsigned long total_sects; //total sections the cm log represents
	unsigned long allowed_sects; //the maximum number of sections that may be allocated at once
	struct cow_section *sects; //pointer to the array of sections of mappings
};

struct snap_device{
	unsigned int sd_minor; //minor number of the snapshot
	unsigned long sd_state; //current state of the snapshot
	unsigned long sd_falloc_size; //space allocated to the cow file (in megabytes)
	unsigned long sd_cache_size; //maximum cache size (in bytes)
	atomic_t sd_refs; //number of users who have this device open
	atomic_t sd_fail_code; //failure return code
	sector_t sd_sect_off; //starting sector of base block device
	sector_t sd_size; //size of device in sectors
	struct request_queue *sd_queue; //snap device request queue
	struct gendisk *sd_gd; //snap device gendisk
	struct block_device *sd_base_dev; //device being snapshot
	char *sd_bdev_path; //base device file path
	struct cow_manager *sd_cow; //cow manager
	char *sd_cow_path; //cow file path
	struct inode *sd_cow_inode; //cow file inode
	make_request_fn *sd_orig_mrf; //block device's original make request function
	struct task_struct *sd_cow_thread; //thread for handling file read/writes
	struct bio_queue sd_cow_bios; //list of outstanding cow bios
	struct task_struct *sd_mrf_thread; //thread for handling file read/writes
	struct bio_queue sd_orig_bios; //list of outstanding original bios
	struct sset_queue sd_pending_ssets; //list of outstanding sector sets
#ifndef HAVE_BIOSET_INIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
	struct bio_set *sd_bioset; //allocation pool for bios
#else
	struct bio_set sd_bioset; //allocation pool for bios
#endif
	atomic64_t sd_submitted_cnt; //count of read clones submitted to underlying driver
	atomic64_t sd_received_cnt; //count of read clones submitted to underlying driver
};

static long ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#ifdef HAVE_BDOPS_OPEN_INODE
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int snap_open(struct inode *inode, struct file *filp);
static int snap_release(struct inode *inode, struct file *filp);
#elif defined HAVE_BDOPS_OPEN_INT
//#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
static int snap_open(struct block_device *bdev, fmode_t mode);
static int snap_release(struct gendisk *gd, fmode_t mode);
#else
static int snap_open(struct block_device *bdev, fmode_t mode);
static void snap_release(struct gendisk *gd, fmode_t mode);
#endif


#ifdef HAVE_BIO_ENDIO_INT
static int on_bio_read_complete(struct bio *bio, unsigned int bytes, int error);
#elif !defined HAVE_BIO_ENDIO_1
static void on_bio_read_complete(struct bio *bio, int err);
#else
static void on_bio_read_complete(struct bio *bio);
#endif

static int dattobd_proc_show(struct seq_file *m, void *v);
static void *dattobd_proc_start(struct seq_file *m, loff_t *pos);
static void *dattobd_proc_next(struct seq_file *m, void *v, loff_t *pos);
static void dattobd_proc_stop(struct seq_file *m, void *v);
static int dattobd_proc_open(struct inode *inode, struct file *filp);
static int dattobd_proc_release(struct inode *inode, struct file *file);

static const struct block_device_operations snap_ops = {
	.owner = THIS_MODULE,
	.open = snap_open,
	.release = snap_release,
};

static const struct file_operations snap_control_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ctrl_ioctl,
	.compat_ioctl = ctrl_ioctl,
	.open = nonseekable_open,
	.llseek = noop_llseek,
};

static struct miscdevice snap_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = CONTROL_DEVICE_NAME,
	.fops = &snap_control_fops,
};

static const struct seq_operations dattobd_seq_proc_ops = {
	.start = dattobd_proc_start,
	.next = dattobd_proc_next,
	.stop = dattobd_proc_stop,
	.show = dattobd_proc_show,
};

static const struct file_operations dattobd_proc_fops = {
	.owner = THIS_MODULE,
	.open = dattobd_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = dattobd_proc_release,
};

static int major;
static struct mutex ioctl_mutex;
static unsigned int highest_minor, lowest_minor;
static struct snap_device **snap_devices;
static struct proc_dir_entry *info_proc;
static void **system_call_table = NULL;

static asmlinkage long (*orig_mount)(char __user *, char __user *, char __user *, unsigned long, void __user *);
static asmlinkage long (*orig_umount)(char __user *, int);
#ifdef HAVE_SYS_OLDUMOUNT
static asmlinkage long (*orig_oldumount)(char __user *);
#endif

/*******************************ATOMIC FUNCTIONS******************************/

static inline int tracer_read_fail_state(const struct snap_device *dev){
	smp_mb();
	return atomic_read(&dev->sd_fail_code);
}

static inline void tracer_set_fail_state(struct snap_device *dev, int error){
	smp_mb();
	(void)atomic_cmpxchg(&dev->sd_fail_code, 0, error);
	smp_mb();
}

/************************IOCTL COPY FROM USER FUNCTIONS************************/

static int copy_string_from_user(const char __user *data, char **out_ptr){
	int ret;
	char *str;

	if(!data){
		*out_ptr = NULL;
		return 0;
	}

	str = strndup_user(data, PAGE_SIZE);
	if(IS_ERR(str)){
		ret = PTR_ERR(str);
		goto error;
	}

	*out_ptr = str;
	return 0;

error:
	LOG_ERROR(ret, "error copying string from user space");
	*out_ptr = NULL;
	return ret;
}

static int get_setup_params(const struct setup_params __user *in, unsigned int *minor, char **bdev_name, char **cow_path, unsigned long *fallocated_space, unsigned long *cache_size){
	int ret;
	struct setup_params params;

	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct setup_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying setup_params struct from user space");
		goto error;
	}

	ret = copy_string_from_user((char __user *)params.bdev, bdev_name);
	if(ret) goto error;

	if(!*bdev_name){
		ret = -EINVAL;
		LOG_ERROR(ret, "NULL bdev given");
		goto error;
	}

	ret = copy_string_from_user((char __user *)params.cow, cow_path);
	if(ret) goto error;

	if(!*cow_path){
		ret = -EINVAL;
		LOG_ERROR(ret, "NULL cow given");
		goto error;
	}

	*minor = params.minor;
	*fallocated_space = params.fallocated_space;
	*cache_size = params.cache_size;
	return 0;

error:
	LOG_ERROR(ret, "error copying setup_params from user space");
	if(*bdev_name) kfree(*bdev_name);
	if(*cow_path) kfree(*cow_path);

	*bdev_name = NULL;
	*cow_path = NULL;
	*minor = 0;
	*fallocated_space = 0;
	*cache_size = 0;
	return ret;
}

static int get_reload_params(const struct reload_params __user *in, unsigned int *minor, char **bdev_name, char **cow_path, unsigned long *cache_size){
	int ret;
	struct reload_params params;

	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct reload_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying reload_params struct from user space");
		goto error;
	}

	ret = copy_string_from_user((char __user *)params.bdev, bdev_name);
	if(ret) goto error;

	if(!*bdev_name){
		ret = -EINVAL;
		LOG_ERROR(ret, "NULL bdev given");
		goto error;
	}

	ret = copy_string_from_user((char __user *)params.cow, cow_path);
	if(ret) goto error;

	if(!*cow_path){
		ret = -EINVAL;
		LOG_ERROR(ret, "NULL cow given");
		goto error;
	}

	*minor = params.minor;
	*cache_size = params.cache_size;
	return 0;

error:
	LOG_ERROR(ret, "error copying reload_params from user space");
	if(*bdev_name) kfree(*bdev_name);
	if(*cow_path) kfree(*cow_path);

	*bdev_name = NULL;
	*cow_path = NULL;
	*minor = 0;
	*cache_size = 0;
	return ret;
}

static int get_transition_snap_params(const struct transition_snap_params __user *in, unsigned int *minor, char **cow_path, unsigned long *fallocated_space){
	int ret;
	struct transition_snap_params params;

	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct transition_snap_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying transition_snap_params struct from user space");
		goto error;
	}

	ret = copy_string_from_user((char __user *)params.cow, cow_path);
	if(ret) goto error;

	if(!*cow_path){
		ret = -EINVAL;
		LOG_ERROR(ret, "NULL cow given");
		goto error;
	}

	*minor = params.minor;
	*fallocated_space = params.fallocated_space;
	return 0;

error:
	LOG_ERROR(ret, "error copying transition_snap_params from user space");
	if(*cow_path) kfree(*cow_path);

	*cow_path = NULL;
	*minor = 0;
	*fallocated_space = 0;
	return ret;
}

static int get_reconfigure_params(const struct reconfigure_params __user *in, unsigned int *minor, unsigned long *cache_size){
	int ret;
	struct reconfigure_params params;

	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct reconfigure_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying reconfigure_params struct from user space");
		goto error;
	}

	*minor = params.minor;
	*cache_size = params.cache_size;
	return 0;

error:
	LOG_ERROR(ret, "error copying reconfigure_params from user space");

	*minor = 0;
	*cache_size = 0;
	return ret;
}

/******************************TASK WORK FUNCTIONS*******************************/
//reimplementation of task_work_run() to force fput() and mntput() to perform their work synchronously
#ifdef HAVE_TASK_STRUCT_TASK_WORKS_HLIST
static void task_work_flush(void){
	struct task_struct *task = current;
	struct hlist_head task_works;
	struct hlist_node *pos;

	raw_spin_lock_irq(&task->pi_lock);
	hlist_move_list(&task->task_works, &task_works);
	raw_spin_unlock_irq(&task->pi_lock);

	if(unlikely(hlist_empty(&task_works))) return;

	for(pos = task_works.first; pos->next; pos = pos->next);

	for(;;){
		struct hlist_node **pprev = pos->pprev;
		struct task_work *twork = container_of(pos, struct task_work, hlist);
		twork->func(twork);

		if(pprev == &task_works.first) break;
		pos = container_of(pprev, struct hlist_node, next);
	}
}
#elif defined HAVE_TASK_STRUCT_TASK_WORKS_CB_HEAD
static void task_work_flush(void){
	struct task_struct *task = current;
	struct callback_head *work, *head, *next;

	for(;;){
		do{
			work = ACCESS_ONCE(task->task_works);
			head = NULL; //current should not be PF_EXITING
		}while(cmpxchg(&task->task_works, work, head) != work);

		if(!work) break;

		raw_spin_lock_irq(&task->pi_lock);
		raw_spin_unlock_irq(&task->pi_lock);

		head = NULL;
		do{
			next = work->next;
			work->next = head;
			head = work;
			work = next;
		}while(work);

		work = head;
		do{
			next = work->next;
			work->func(work);
			work = next;
			cond_resched();
		}while(work);
	}
}
#else
	#define task_work_flush()
#endif

/******************************FILE OPERATIONS*******************************/

static inline void file_close(struct file *f){
	filp_close(f, NULL);
}

static int file_open(const char *filename, int flags, struct file **filp){
	int ret;
	struct file *f;

	f = filp_open(filename, flags | O_RDWR | O_LARGEFILE, 0);
	if(!f){
		ret = -EFAULT;
		LOG_ERROR(ret, "error creating/opening file '%s' (null pointer)", filename);
		goto error;
	}else if(IS_ERR(f)){
		ret = PTR_ERR(f);
		f = NULL;
		LOG_ERROR(ret, "error creating/opening file '%s' - %d", filename, ret);
		goto error;
	}else if(!S_ISREG(dattobd_get_dentry(f)->d_inode->i_mode)){
		ret = -EINVAL;
		LOG_ERROR(ret, "'%s' is not a regular file", filename);
		goto error;
	}
	f->f_mode |= FMODE_NONOTIFY;

	*filp = f;
	return 0;

error:
	LOG_ERROR(ret, "error opening file");
	if(f) file_close(f);

	*filp = NULL;
	return ret;
}

#if !defined(HAVE___DENTRY_PATH) && !defined(HAVE_DENTRY_PATH_RAW)
static int dentry_get_relative_pathname(struct dentry *dentry, char **buf, int *len_res){
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
static int dentry_get_relative_pathname(struct dentry *dentry, char **buf, int *len_res){
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
		goto error;
	}

	len = page_buf + PAGE_SIZE - pathname;
	final_buf = kmalloc(len, GFP_KERNEL);
	if(!final_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating pathname for dentry");
		goto error;
	}

	strncpy(final_buf, pathname, len);
	free_page((unsigned long)page_buf);

	*buf = final_buf;
	if(len_res) *len_res = len;
	return 0;

error:
	LOG_ERROR(ret, "error converting dentry to relative path name");
	if(final_buf) kfree(final_buf);
	if(page_buf) free_page((unsigned long)page_buf);

	*buf = NULL;
	if(len_res) *len_res = 0;
	return ret;
}
#endif

static int path_get_absolute_pathname(const struct path *path, char **buf, int *len_res){
	int ret, len;
	char *pathname, *page_buf, *final_buf = NULL;

	page_buf = (char *)__get_free_page(GFP_KERNEL);
	if(!page_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating page for absolute pathname");
		goto error;
	}

	pathname = dattobd_d_path(path, page_buf, PAGE_SIZE);
	if(IS_ERR(pathname)){
		ret = PTR_ERR(pathname);
		pathname = NULL;
		LOG_ERROR(ret, "error fetching absolute pathname");
		goto error;
	}

	len = page_buf + PAGE_SIZE - pathname;
	final_buf = kmalloc(len, GFP_KERNEL);
	if(!final_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating buffer for absolute pathname");
		goto error;
	}

	strncpy(final_buf, pathname, len);
	free_page((unsigned long)page_buf);

	*buf = final_buf;
	if(len_res) *len_res = len;
	return 0;

error:
	LOG_ERROR(ret, "error getting absolute pathname from path");
	if(final_buf) kfree(final_buf);
	if(page_buf) free_page((unsigned long)page_buf);

	*buf = NULL;
	if(len_res) *len_res = 0;
	return ret;
}

static int file_get_absolute_pathname(const struct file *filp, char **buf, int *len_res){
	struct path path;
	int ret;

	path.mnt = dattobd_get_mnt(filp);
	path.dentry = dattobd_get_dentry(filp);

	ret = path_get_absolute_pathname(&path, buf, len_res);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error converting file to absolute pathname");
	*buf = NULL;
	*len_res = 0;

	return ret;
}

static int pathname_to_absolute(const char *pathname, char **buf, int *len_res){
	int ret;
	struct path path = {};

	ret = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if(ret){
		LOG_ERROR(ret, "error finding path for pathname");
		return ret;
	}

	ret = path_get_absolute_pathname(&path, buf, len_res);
	if(ret) goto error;

	path_put(&path);
	return 0;

error:
	LOG_ERROR(ret, "error converting pathname to absolute pathname");
	path_put(&path);
	return ret;
}

static int pathname_concat(const char *pathname1, const char *pathname2, char **path_out){
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

static int user_mount_pathname_concat(const char __user *user_mount_path, const char *rel_path, char **path_out){
	int ret;
	char *mount_path;

	ret = copy_string_from_user(user_mount_path, &mount_path);
	if(ret) goto error;

	ret = pathname_concat(mount_path, rel_path, path_out);
	if(ret) goto error;

	kfree(mount_path);
	return 0;

error:
	LOG_ERROR(ret, "error concatenating mount path to relative path");
	if(mount_path) kfree(mount_path);

	*path_out = NULL;
	return ret;
}

static int file_io(struct file *filp, int is_write, void *buf, sector_t offset, unsigned long len){
	ssize_t ret;
	loff_t off = (loff_t)offset;

	if(is_write) ret = dattobd_kernel_write(filp, buf, len, &off);
	else ret = dattobd_kernel_read(filp, buf, len, &off);

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
#define file_write(filp, buf, offset, len) file_io(filp, 1, buf, offset, len)
#define file_read(filp, buf, offset, len) file_io(filp, 0, buf, offset, len)

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

	ret = dattobd_should_remove_suid(dentry);
	if(ret) newattrs.ia_valid |= ret | ATTR_FORCE;

	dattobd_inode_lock(dentry->d_inode);
#ifdef HAVE_NOTIFY_CHANGE_2
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
	ret = notify_change(dentry, &newattrs);
#else
	ret = notify_change(dentry, &newattrs, NULL);
#endif
	dattobd_inode_unlock(dentry->d_inode);

	return ret;
}

static int file_truncate(struct file *filp, loff_t len){
	struct inode *inode;
	struct dentry *dentry;
	int ret;

	dentry = dattobd_get_dentry(filp);
	inode = dentry->d_inode;

	ret = locks_verify_truncate(inode, filp, len);
	if(ret){
		LOG_ERROR(ret, "error verifying truncation is possible");
		goto error;
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
		goto error;
	}

	return 0;

error:
	LOG_ERROR(ret, "error truncating file");
	return ret;
}

#ifdef HAVE_VFS_FALLOCATE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
	#define real_fallocate(f, offset, length) vfs_fallocate(f, 0, offset, length)
#else
static int real_fallocate(struct file *f, uint64_t offset, uint64_t length){
	int ret;
	loff_t off = offset;
	loff_t len = length;
	#ifndef HAVE_FILE_INODE
	//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	struct inode *inode = dattobd_get_dentry(f)->d_inode;
	#else
	struct inode *inode = file_inode(f);
	#endif

	if(off + len > inode->i_sb->s_maxbytes || off + len < 0) return -EFBIG;

	#if !defined(HAVE_IOPS_FALLOCATE) && !defined(HAVE_FOPS_FALLOCATE)
	//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	return -EOPNOTSUPP;
	#elif defined(HAVE_IOPS_FALLOCATE)
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

static int file_allocate(struct file *f, uint64_t offset, uint64_t length){
	int ret = 0;
	char *page_buf = NULL;
	uint64_t i, write_count;
	char *abs_path = NULL;
	int abs_path_len;

	file_get_absolute_pathname(f, &abs_path, &abs_path_len);

	//try regular fallocate
	ret = real_fallocate(f, offset, length);
	if(ret && ret != -EOPNOTSUPP) goto error;
	else if(!ret) goto out;

	//fallocate isn't supported, fall back on writing zeros
	if(!abs_path) {
		LOG_WARN("fallocate is not supported for this file system, falling back on writing zeros");
	} else {
		LOG_WARN("fallocate is not supported for '%s', falling back on writing zeros", abs_path);
	}

	//allocate page of zeros
	page_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if(!page_buf){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating zeroed page");
		goto error;
	}

	//may write up to a page too much, ok for our use case
	write_count = NUM_SEGMENTS(length, PAGE_SHIFT);

	//if not page aligned, write zeros to that point
	if(offset % PAGE_SIZE != 0){
		ret = file_write(f, page_buf, offset, PAGE_SIZE - (offset % PAGE_SIZE));
		if(ret) goto error;

		offset += PAGE_SIZE - (offset % PAGE_SIZE);
	}

	//write a page of zeros at a time
	for(i = 0; i < write_count; i++){
		ret = file_write(f, page_buf, offset + (PAGE_SIZE * i), PAGE_SIZE);
		if(ret) goto error;
	}

out:
	if(page_buf) free_page((unsigned long)page_buf);
	if(abs_path) kfree(abs_path);

	return 0;

error:
	if(!abs_path){
		LOG_ERROR(ret, "error performing fallocate");
	}else{
		LOG_ERROR(ret, "error performing fallocate on file '%s'", abs_path);
	}

	if(page_buf) free_page((unsigned long)page_buf);
	if(abs_path) kfree(abs_path);

	return ret;
}

static int __file_unlink(struct file *filp, int close, int force){
	int ret = 0;
	struct inode *dir_inode = dattobd_get_dentry(filp)->d_parent->d_inode;
	struct dentry *file_dentry = dattobd_get_dentry(filp);
	struct vfsmount *mnt = dattobd_get_mnt(filp);

	if(d_unlinked(file_dentry)){
		if(close) file_close(filp);
		return 0;
	}

	dget(file_dentry);
	igrab(dir_inode);

	ret = mnt_want_write(mnt);
	if(ret){
		LOG_ERROR(ret, "error getting write access to vfs mount");
		goto mnt_error;
	}

#ifdef HAVE_VFS_UNLINK_2
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
	ret = vfs_unlink(dir_inode, file_dentry);
#else
	ret = vfs_unlink(dir_inode, file_dentry, NULL);
#endif
	if(ret){
		LOG_ERROR(ret, "error unlinking file");
		goto error;
	}

error:
	mnt_drop_write(mnt);

	if(close && (!ret || force)) file_close(filp);

mnt_error:
	iput(dir_inode);
	dput(file_dentry);

	return ret;
}
#define file_unlink(filp) __file_unlink(filp, 0, 0)
#define file_unlink_and_close(filp) __file_unlink(filp, 1, 0)
#define file_unlink_and_close_force(filp) __file_unlink(filp, 1, 1)

/***************************COW MANAGER FUNCTIONS**************************/

static void __cow_free_section(struct cow_manager *cm, unsigned long sect_idx){
	free_pages((unsigned long)cm->sects[sect_idx].mappings, cm->log_sect_pages);
	cm->sects[sect_idx].mappings = NULL;
	cm->allocated_sects--;
}

static int __cow_alloc_section(struct cow_manager *cm, unsigned long sect_idx, int zero){
	if(zero) cm->sects[sect_idx].mappings = (void*)get_zeroed_pages(GFP_KERNEL, cm->log_sect_pages);
	else cm->sects[sect_idx].mappings = (void*)__get_free_pages(GFP_KERNEL, cm->log_sect_pages);

	if(!cm->sects[sect_idx].mappings){
		LOG_ERROR(-ENOMEM, "failed to allocate mappings at index %lu", sect_idx);
		return -ENOMEM;
	}

	cm->sects[sect_idx].has_data = 1;
	cm->allocated_sects++;

	return 0;
}

static int __cow_load_section(struct cow_manager *cm, unsigned long sect_idx){
	int ret;

	ret = __cow_alloc_section(cm, sect_idx, 0);
	if(ret) goto error;

	ret = file_read(cm->filp, cm->sects[sect_idx].mappings, cm->sect_size*sect_idx*8 + COW_HEADER_SIZE, cm->sect_size*8);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error loading section from file");
	if(cm->sects[sect_idx].mappings) __cow_free_section(cm, sect_idx);
	return ret;
}

static int __cow_write_section(struct cow_manager *cm, unsigned long sect_idx){
	int ret;

	ret = file_write(cm->filp, cm->sects[sect_idx].mappings, cm->sect_size*sect_idx*8 + COW_HEADER_SIZE, cm->sect_size*8);
	if(ret){
		LOG_ERROR(ret, "error writing cow manager section to file");
		return ret;
	}

	return 0;
}

static int __cow_sync_and_free_sections(struct cow_manager *cm, unsigned long thresh){
	int ret;
	unsigned long i;

	for(i=0; i<cm->total_sects && (!thresh || cm->allocated_sects > cm->allowed_sects/2); i++){
		if(cm->sects[i].mappings && (!thresh || cm->sects[i].usage <= thresh)){
			ret = __cow_write_section(cm, i);
			if(ret){
				LOG_ERROR(ret, "error writing cow manager section %lu to file", i);
				return ret;
			}

			__cow_free_section(cm, i);
		}
		cm->sects[i].usage = 0;
	}

	return 0;
}

static int __cow_cleanup_mappings(struct cow_manager *cm){
	int ret;
	unsigned long less, greater, i, granularity, thresh = 0;

	//find the max usage of the sections of the cm
	for(i=0; i<cm->total_sects; i++){
		if(cm->sects[i].usage > thresh) thresh = cm->sects[i].usage;
	}

	//find the (approximate) median usage of the sections of the cm
	thresh /= 2;
	granularity = thresh;
	while(granularity > 0){
		granularity = granularity >> 1;
		less = 0;
		greater = 0;
		for(i=0; i<cm->total_sects; i++){
			if(cm->sects[i].usage <= thresh) less++;
			else greater++;
		}

		if(greater > less) thresh += granularity;
		else if(greater < less) thresh -= granularity;
		else break;
	}

	//deallocate sections of the cm with less usage than the median
	ret = __cow_sync_and_free_sections(cm, thresh);
	if(ret){
		LOG_ERROR(ret, "error cleaning cow manager mappings");
		return ret;
	}

	return 0;
}

static int __cow_write_header(struct cow_manager *cm, int is_clean){
	int ret;
	struct cow_header ch;

	if(is_clean) cm->flags |= (1 << COW_CLEAN);
	else cm->flags &= ~(1 << COW_CLEAN);

	ch.magic = COW_MAGIC;
	ch.flags = cm->flags;
	ch.fpos = cm->curr_pos;
	ch.fsize = cm->file_max;
	ch.seqid = cm->seqid;
	memcpy(ch.uuid, cm->uuid, COW_UUID_SIZE);
	ch.version = cm->version;
	ch.nr_changed_blocks = cm->nr_changed_blocks;

	ret = file_write(cm->filp, &ch, 0, sizeof(struct cow_header));
	if(ret){
		LOG_ERROR(ret, "error syncing cow manager header");
		return ret;
	}

	return 0;
}
#define __cow_write_header_dirty(cm) __cow_write_header(cm, 0)
#define __cow_close_header(cm) __cow_write_header(cm, 1)

static int __cow_open_header(struct cow_manager *cm, int index_only, int reset_vmalloc){
	int ret;
	struct cow_header ch;

	ret = file_read(cm->filp, &ch, 0, sizeof(struct cow_header));
	if(ret) goto error;

	if(ch.magic != COW_MAGIC){
		ret = -EINVAL;
		LOG_ERROR(-EINVAL, "bad magic number found in cow file: %lu", ((unsigned long)ch.magic));
		goto error;
	}

	if(!(ch.flags & (1 << COW_CLEAN))){
		ret = -EINVAL;
		LOG_ERROR(-EINVAL, "cow file not left in clean state: %lu", ((unsigned long)ch.flags));
		goto error;
	}

	if(((ch.flags & (1 << COW_INDEX_ONLY)) && !index_only) || (!(ch.flags & (1 << COW_INDEX_ONLY)) && index_only)){
		ret = -EINVAL;
		LOG_ERROR(-EINVAL, "cow file not left in %s state: %lu", ((index_only)? "index only" : "data tracking"), (unsigned long)ch.flags);
		goto error;
	}

	LOG_DEBUG("cow header opened with file pos = %llu, seqid = %llu", ((unsigned long long)ch.fpos), (unsigned long long)ch.seqid);

	if(reset_vmalloc) cm->flags = ch.flags & ~(1 << COW_VMALLOC_UPPER);
	else cm->flags = ch.flags;

	cm->curr_pos = ch.fpos;
	cm->file_max = ch.fsize;
	cm->seqid = ch.seqid;
	memcpy(cm->uuid, ch.uuid, COW_UUID_SIZE);
	cm->version = ch.version;
	cm->nr_changed_blocks = ch.nr_changed_blocks;

	ret = __cow_write_header_dirty(cm);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error opening cow manager header");
	return ret;
}

static void cow_free_members(struct cow_manager *cm){
	unsigned long i;

	if(cm->sects){
		for(i = 0; i < cm->total_sects; i++){
			if(cm->sects[i].mappings) free_pages((unsigned long)cm->sects[i].mappings, cm->log_sect_pages);
		}

		if(cm->flags & (1 << COW_VMALLOC_UPPER)) vfree(cm->sects);
		else kfree(cm->sects);

		cm->sects = NULL;
	}

	if(cm->filp){
		file_unlink_and_close_force(cm->filp);
		cm->filp = NULL;
	}
}

static void cow_free(struct cow_manager *cm){
	cow_free_members(cm);
	kfree(cm);
}

static int cow_sync_and_free(struct cow_manager *cm){
	int ret;

	ret = __cow_sync_and_free_sections(cm, 0);
	if(ret) goto error;

	ret = __cow_close_header(cm);
	if(ret) goto error;

	if(cm->filp) file_close(cm->filp);

	if(cm->sects){
		if(cm->flags & (1 << COW_VMALLOC_UPPER)) vfree(cm->sects);
		else kfree(cm->sects);
	}

	kfree(cm);

	return 0;

error:
	LOG_ERROR(ret, "error while syncing and freeing cow manager");
	cow_free(cm);
	return ret;
}

static int cow_sync_and_close(struct cow_manager *cm){
	int ret;

	ret = __cow_sync_and_free_sections(cm, 0);
	if(ret) goto error;

	ret = __cow_close_header(cm);
	if(ret) goto error;

	if(cm->filp) file_close(cm->filp);
	cm->filp = NULL;

	return 0;

error:
	LOG_ERROR(ret, "error while syncing and closing cow manager");
	cow_free_members(cm);
	return ret;
}

static int cow_reopen(struct cow_manager *cm, const char *pathname){
	int ret;

	LOG_DEBUG("reopening cow file");
	ret = file_open(pathname, 0, &cm->filp);
	if(ret) goto error;

	LOG_DEBUG("opening cow header");
	ret = __cow_open_header(cm, (cm->flags & (1 << COW_INDEX_ONLY)), 0);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error reopening cow manager");
	if(cm->filp) file_close(cm->filp);
	cm->filp = NULL;

	return ret;
}

static unsigned long __cow_calculate_allowed_sects(unsigned long cache_size, unsigned long total_sects){
	if(cache_size <= (total_sects * sizeof(struct cow_section))) return 0;
	else return (cache_size - (total_sects * sizeof(struct cow_section))) / (COW_SECTION_SIZE * 8);
}

static int cow_reload(const char *path, uint64_t elements, unsigned long sect_size, unsigned long cache_size, int index_only, struct cow_manager **cm_out){
	int ret;
	unsigned long i;
	struct cow_manager *cm;

	LOG_DEBUG("allocating cow manager");
	cm = kzalloc(sizeof(struct cow_manager), GFP_KERNEL);
	if(!cm){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating cow manager");
		goto error;
	}

	LOG_DEBUG("opening cow file");
	ret = file_open(path, 0, &cm->filp);
	if(ret) goto error;

	cm->allocated_sects = 0;
	cm->sect_size = sect_size;
	cm->log_sect_pages = get_order(sect_size*8);
	cm->total_sects = NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
	cm->allowed_sects = __cow_calculate_allowed_sects(cache_size, cm->total_sects);
	cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size*8));

	ret = __cow_open_header(cm, index_only, 1);
	if(ret) goto error;

	LOG_DEBUG("allocating cow manager array (%lu sections)", cm->total_sects);
	cm->sects = kzalloc((cm->total_sects) * sizeof(struct cow_section), GFP_KERNEL | __GFP_NOWARN);
	if(!cm->sects){
		//try falling back to vmalloc
		cm->flags |= (1 << COW_VMALLOC_UPPER);
		cm->sects = vzalloc((cm->total_sects) * sizeof(struct cow_section));
		if(!cm->sects){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating cow manager sects array");
			goto error;
		}
	}

	for(i=0; i<cm->total_sects; i++){
		cm->sects[i].has_data = 1;
	}

	*cm_out = cm;
	return 0;

error:
	LOG_ERROR(ret, "error during cow manager initialization");
	if(cm->filp) file_close(cm->filp);

	if(cm->sects){
		if(cm->flags & (1 << COW_VMALLOC_UPPER)) vfree(cm->sects);
		else kfree(cm->sects);
	}

	if(cm) kfree(cm);

	*cm_out = NULL;
	return ret;
}

static int cow_init(const char *path, uint64_t elements, unsigned long sect_size, unsigned long cache_size, uint64_t file_max, const uint8_t *uuid, uint64_t seqid, struct cow_manager **cm_out){
	int ret;
	struct cow_manager *cm;

	LOG_DEBUG("allocating cow manager, seqid = %llu", (unsigned long long)seqid);
	cm = kzalloc(sizeof(struct cow_manager), GFP_KERNEL);
	if(!cm){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating cow manager");
		goto error;
	}

	LOG_DEBUG("creating cow file");
	ret = file_open(path, O_CREAT | O_TRUNC, &cm->filp);
	if(ret) goto error;

	cm->version = COW_VERSION_CHANGED_BLOCKS;
	cm->nr_changed_blocks = 0;
	cm->flags = 0;
	cm->allocated_sects = 0;
	cm->file_max = file_max;
	cm->sect_size = sect_size;
	cm->seqid = seqid;
	cm->log_sect_pages = get_order(sect_size*8);
	cm->total_sects = NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
	cm->allowed_sects = __cow_calculate_allowed_sects(cache_size, cm->total_sects);
	cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size*8));
	cm->curr_pos = cm->data_offset / COW_BLOCK_SIZE;

	if(uuid) memcpy(cm->uuid, uuid, COW_UUID_SIZE);
	else generate_random_uuid(cm->uuid);

	LOG_DEBUG("allocating cow manager array (%lu sections)", cm->total_sects);
	cm->sects = kzalloc((cm->total_sects) * sizeof(struct cow_section), GFP_KERNEL | __GFP_NOWARN);
	if(!cm->sects){
		//try falling back to vmalloc
		cm->flags |= (1 << COW_VMALLOC_UPPER);
		cm->sects = vzalloc((cm->total_sects) * sizeof(struct cow_section));
		if(!cm->sects){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating cow manager sects array");
			goto error;
		}
	}

	LOG_DEBUG("allocating cow file (%llu bytes)", (unsigned long long)file_max);
	ret = file_allocate(cm->filp, 0, file_max);
	if(ret) goto error;

	ret = __cow_write_header_dirty(cm);
	if(ret) goto error;

	*cm_out = cm;
	return 0;

error:
	LOG_ERROR(ret, "error during cow manager initialization");
	if(cm->filp) file_unlink_and_close(cm->filp);

	if(cm->sects){
		if(cm->flags & (1 << COW_VMALLOC_UPPER)) vfree(cm->sects);
		else kfree(cm->sects);
	}

	if(cm) kfree(cm);

	*cm_out = NULL;
	return ret;
}

static int cow_truncate_to_index(struct cow_manager *cm){
	//truncate the cow file to just the index
	cm->flags |= (1 << COW_INDEX_ONLY);
	return file_truncate(cm->filp, cm->data_offset);
}

static void cow_modify_cache_size(struct cow_manager *cm, unsigned long cache_size){
	cm->allowed_sects = __cow_calculate_allowed_sects(cache_size, cm->total_sects);
}

static int cow_read_mapping(struct cow_manager *cm, uint64_t pos, uint64_t *out){
	int ret;
	uint64_t sect_idx = pos;
	unsigned long sect_pos = do_div(sect_idx, cm->sect_size);

	cm->sects[sect_idx].usage++;

	if(!cm->sects[sect_idx].mappings){
		if(!cm->sects[sect_idx].has_data){
			*out = 0;
			return 0;
		}else{
			ret = __cow_load_section(cm, sect_idx);
			if(ret) goto error;
		}
	}

	*out = cm->sects[sect_idx].mappings[sect_pos];

	if(cm->allocated_sects > cm->allowed_sects){
		ret = __cow_cleanup_mappings(cm);
		if(ret) goto error;
	}

	return 0;

error:
	LOG_ERROR(ret, "error reading cow mapping");
	return ret;
}

static int __cow_write_mapping(struct cow_manager *cm, uint64_t pos, uint64_t val){
	int ret;
	uint64_t sect_idx = pos;
	unsigned long sect_pos = do_div(sect_idx, cm->sect_size);

	cm->sects[sect_idx].usage++;

	if(!cm->sects[sect_idx].mappings){
		if(!cm->sects[sect_idx].has_data){
			ret = __cow_alloc_section(cm, sect_idx, 1);
			if(ret) goto error;
		}else{
			ret = __cow_load_section(cm, sect_idx);
			if(ret) goto error;
		}
	}

	if(cm->version >= COW_VERSION_CHANGED_BLOCKS && !cm->sects[sect_idx].mappings[sect_pos]) cm->nr_changed_blocks++;

	cm->sects[sect_idx].mappings[sect_pos] = val;

	if(cm->allocated_sects > cm->allowed_sects){
		ret = __cow_cleanup_mappings(cm);
		if(ret) goto error;
	}

	return 0;

error:
	LOG_ERROR(ret, "error writing cow mapping");
	return ret;
}
#define __cow_write_current_mapping(cm, pos) __cow_write_mapping(cm, pos, (cm)->curr_pos)
#define cow_write_filler_mapping(cm, pos) __cow_write_mapping(cm, pos, 1)

static int __cow_write_data(struct cow_manager *cm, void *buf){
	int ret;
	char *abs_path = NULL;
	int abs_path_len;
	uint64_t curr_size = cm->curr_pos * COW_BLOCK_SIZE;

	if(curr_size >= cm->file_max){
		ret = -EFBIG;

		file_get_absolute_pathname(cm->filp, &abs_path, &abs_path_len);
		if(!abs_path){
			LOG_ERROR(ret, "cow file max size exceeded (%llu/%llu)", curr_size, cm->file_max);
		}else{
			LOG_ERROR(ret, "cow file '%s' max size exceeded (%llu/%llu)", abs_path, curr_size, cm->file_max);
			kfree(abs_path);
		}

		goto error;
	}

	ret = file_write(cm->filp, buf, curr_size, COW_BLOCK_SIZE);
	if(ret) goto error;

	cm->curr_pos++;

	return 0;

error:
	LOG_ERROR(ret, "error writing cow data");
	return ret;
}

static int cow_write_current(struct cow_manager *cm, uint64_t block, void *buf){
	int ret;
	uint64_t block_mapping;

	//read this mapping from the cow manager
	ret = cow_read_mapping(cm, block, &block_mapping);
	if(ret) goto error;

	//if the block mapping already exists return so we don't overwrite it
	if(block_mapping) return 0;

	//write the mapping
	ret = __cow_write_current_mapping(cm, block);
	if(ret) goto error;

	//write the data
	ret = __cow_write_data(cm, buf);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error writing cow data and mapping");
	return ret;
}

static int cow_read_data(struct cow_manager *cm, void *buf, uint64_t block_pos, unsigned long block_off, unsigned long len){
	int ret;

	if(block_off >= COW_BLOCK_SIZE) return -EINVAL;

	ret = file_read(cm->filp, buf, (block_pos * COW_BLOCK_SIZE) + block_off, len);
	if(ret){
		LOG_ERROR(ret, "error reading cow data");
		return ret;
	}

	return 0;
}

/***************************SECTOR_SET LIST FUNCTIONS**************************/

static inline void sset_list_init(struct sset_list *sl){
	sl->head = sl->tail = NULL;
}

static inline int sset_list_empty(const struct sset_list *sl){
	return sl->head == NULL;
}

static void sset_list_add(struct sset_list *sl, struct sector_set *sset){
	sset->next = NULL;
	if(sl->tail) sl->tail->next = sset;
	else sl->head = sset;
	sl->tail = sset;
}

static struct sector_set *sset_list_pop(struct sset_list *sl){
	struct sector_set *sset = sl->head;

	if(sset) {
		sl->head = sl->head->next;
		if(!sl->head) sl->tail = NULL;
		sset->next = NULL;
	}

	return sset;
}

/****************************BIO QUEUE FUNCTIONS****************************/

static void bio_queue_init(struct bio_queue *bq){
	bio_list_init(&bq->bios);
	spin_lock_init(&bq->lock);
	init_waitqueue_head(&bq->event);
}

static int bio_queue_empty(const struct bio_queue *bq){
	return bio_list_empty(&bq->bios);
}

static void bio_queue_add(struct bio_queue *bq, struct bio *bio){
	unsigned long flags;

	spin_lock_irqsave(&bq->lock, flags);
	bio_list_add(&bq->bios, bio);
	spin_unlock_irqrestore(&bq->lock, flags);
	wake_up(&bq->event);
}

static struct bio *bio_queue_dequeue(struct bio_queue *bq){
	unsigned long flags;
	struct bio *bio;

	spin_lock_irqsave(&bq->lock, flags);
	bio = bio_list_pop(&bq->bios);
	spin_unlock_irqrestore(&bq->lock, flags);

	return bio;
}

static int bio_overlap(const struct bio *bio1, const struct bio *bio2){
	return max(bio_sector(bio1), bio_sector(bio2)) <= min(bio_sector(bio1) + (bio_size(bio1) / SECTOR_SIZE), bio_sector(bio2) + (bio_size(bio2) / SECTOR_SIZE));
}

static struct bio *bio_queue_dequeue_delay_read(struct bio_queue *bq){
	unsigned long flags;
	struct bio *bio, *tmp, *prev = NULL;

	spin_lock_irqsave(&bq->lock, flags);
	bio = bio_list_pop(&bq->bios);

	if(!bio_data_dir(bio)){
		bio_list_for_each(tmp, &bq->bios){
			if(bio_data_dir(tmp) && bio_overlap(bio, tmp)){
				if(prev) prev->bi_next = bio;
				else bq->bios.head = bio;

				if(bq->bios.tail == tmp) bq->bios.tail = bio;

				bio->bi_next = tmp->bi_next;
				tmp->bi_next = NULL;
				bio = tmp;

				goto out;
			}
			prev = tmp;
		}
	}

out:
	spin_unlock_irqrestore(&bq->lock, flags);

	return bio;
}

/****************************SSET QUEUE FUNCTIONS****************************/

static void sset_queue_init(struct sset_queue *sq){
	sset_list_init(&sq->ssets);
	spin_lock_init(&sq->lock);
	init_waitqueue_head(&sq->event);
}

static int sset_queue_empty(const struct sset_queue *sq){
	return sset_list_empty(&sq->ssets);
}

static void sset_queue_add(struct sset_queue *sq, struct sector_set *sset){
	unsigned long flags;

	spin_lock_irqsave(&sq->lock, flags);
	sset_list_add(&sq->ssets, sset);
	spin_unlock_irqrestore(&sq->lock, flags);
	wake_up(&sq->event);
}

static struct sector_set *sset_queue_dequeue(struct sset_queue *sq){
	unsigned long flags;
	struct sector_set *sset;

	spin_lock_irqsave(&sq->lock, flags);
	sset = sset_list_pop(&sq->ssets);
	spin_unlock_irqrestore(&sq->lock, flags);

	return sset;
}

/***************************TRACING PARAMS FUNCTIONS**************************/

static int tp_alloc(struct snap_device *dev, struct bio *bio, struct tracing_params **tp_out){
	struct tracing_params *tp;

	tp = kzalloc(1 * sizeof(struct tracing_params), GFP_NOIO);
	if(!tp){
		LOG_ERROR(-ENOMEM, "error allocating tracing parameters struct");
		*tp_out = tp;
		return -ENOMEM;
	}

	tp->dev = dev;
	tp->orig_bio = bio;
	atomic_set(&tp->refs, 1);

	*tp_out = tp;
	return 0;
}

static void tp_get(struct tracing_params *tp){
	atomic_inc(&tp->refs);
}

static void tp_put(struct tracing_params *tp){
	//drop a reference to the tp
	if(atomic_dec_and_test(&tp->refs)){
		//if there are no references left, its safe to release the orig_bio
		bio_queue_add(&tp->dev->sd_orig_bios, tp->orig_bio);
		kfree(tp);
	}
}

/****************************BIO HELPER FUNCTIONS*****************************/

static inline struct inode *page_get_inode(struct page *pg){
	if(!pg) return NULL;

	// page_mapping() was not exported until 4.8, use compound_head() instead
#ifdef HAVE_COMPOUND_HEAD
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(2.6.22)
	pg = compound_head(pg);
#endif
	if(PageAnon(pg)) return NULL;
	if(!pg->mapping) return NULL;
	return pg->mapping->host;
}

static int bio_needs_cow(struct bio *bio, struct inode *inode){
	bio_iter_t iter;
	bio_iter_bvec_t bvec;

#ifdef HAVE_ENUM_REQ_OPF
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	if(bio_op(bio) == REQ_OP_WRITE_ZEROES) return 1;
#endif

	//check the inode of each page return true if it does not match our cow file
	bio_for_each_segment(bvec, bio, iter){
		if(page_get_inode(bio_iter_page(bio, iter)) != inode) return 1;
	}

	return 0;
}

#ifndef HAVE_BIO_BI_POOL
static void bio_destructor_tp(struct bio *bio){
	struct tracing_params *tp = bio->bi_private;
	bio_free(bio, dev_bioset(tp->dev));
}

static void bio_destructor_snap_dev(struct bio *bio){
	struct snap_device *dev = bio->bi_private;
	bio_free(bio, dev_bioset(dev));
}
#endif

static void bio_free_clone(struct bio *bio){
	int i;

	for(i = 0; i < bio->bi_vcnt; i++){
		if(bio->bi_io_vec[i].bv_page) __free_page(bio->bi_io_vec[i].bv_page);
	}
	bio_put(bio);
}

static int bio_make_read_clone(struct bio_set *bs, struct tracing_params *tp, struct bio *orig_bio, sector_t sect, unsigned int pages, struct bio **bio_out, unsigned int *bytes_added){
	int ret;
	struct bio *new_bio;
	struct page *pg;
	unsigned int i, bytes, total = 0, actual_pages = (pages > BIO_MAX_PAGES)? BIO_MAX_PAGES : pages;

	//allocate bio clone
	new_bio = bio_alloc_bioset(GFP_NOIO, actual_pages, bs);
	if(!new_bio){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating bio clone - bs = %p, pages = %u", bs, pages);
		goto error;
	}

#ifndef HAVE_BIO_BI_POOL
	new_bio->bi_destructor = bio_destructor_tp;
#endif

	//populate read bio
	tp_get(tp);
	new_bio->bi_private = tp;
	new_bio->bi_end_io = on_bio_read_complete;
	dattobd_bio_copy_dev(new_bio, orig_bio);
	dattobd_set_bio_ops(new_bio, REQ_OP_READ, 0);
	bio_sector(new_bio) = sect;
	bio_idx(new_bio) = 0;

	//fill the bio with pages
	for(i = 0; i < actual_pages; i++){
		//allocate a page and add it to our bio
		pg = alloc_page(GFP_NOIO);
		if(!pg){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating read bio page %u", i);
			goto error;
		}

		//add the page to the bio
		bytes = bio_add_page(new_bio, pg, PAGE_SIZE, 0);
		if(bytes != PAGE_SIZE){
			__free_page(pg);
			break;
		}

		total += bytes;
	}

	*bytes_added = total;
	*bio_out = new_bio;
	return 0;

error:
	if(ret) LOG_ERROR(ret, "error creating read clone of write bio");
	if(new_bio) bio_free_clone(new_bio);

	*bytes_added = 0;
	*bio_out = NULL;
	return ret;
}

/*******************BIO / SECTOR_SET PROCESSING LOGIC***********************/

static int snap_read_bio_get_mode(const struct snap_device *dev, struct bio *bio, int *mode){
	int ret, start_mode = 0;
	bio_iter_t iter;
	bio_iter_bvec_t bvec;
	unsigned int bytes;
	uint64_t block_mapping, curr_byte, curr_end_byte = bio_sector(bio) * SECTOR_SIZE;

	bio_for_each_segment(bvec, bio, iter){
		//reset the number of bytes we have traversed for this bio_vec
		bytes = 0;

		//while we still have data left to be written into the page
		while(bytes < bio_iter_len(bio, iter)){
			//find the start and stop byte for our next write
			curr_byte = curr_end_byte;
			curr_end_byte += min(COW_BLOCK_SIZE - (curr_byte % COW_BLOCK_SIZE), ((uint64_t)bio_iter_len(bio, iter)));

			//check if the mapping exists
			ret = cow_read_mapping(dev->sd_cow, curr_byte / COW_BLOCK_SIZE, &block_mapping);
			if(ret) goto error;

			if(!start_mode && block_mapping) start_mode = READ_MODE_COW_FILE;
			else if(!start_mode && !block_mapping) start_mode = READ_MODE_BASE_DEVICE;
			else if((start_mode == READ_MODE_COW_FILE && !block_mapping) || (start_mode == READ_MODE_BASE_DEVICE && block_mapping)){
				*mode = READ_MODE_MIXED;
				return 0;
			}

			//increment the number of bytes we have written
			bytes += curr_end_byte - curr_byte;
		}
	}

	*mode = start_mode;
	return 0;

error:
	LOG_ERROR(ret, "error finding read mode");
	return ret;
}

static int snap_handle_read_bio(const struct snap_device *dev, struct bio *bio){
	int ret, mode;
	bio_iter_t iter;
	bio_iter_bvec_t bvec;
	void *orig_private;
	bio_end_io_t *orig_end_io;
	char *data;
	sector_t bio_orig_sect, cur_block, cur_sect;
	unsigned int bio_orig_idx, bio_orig_size;
	uint64_t block_mapping, bytes_to_copy, block_off, bvec_off;

	//save the original state of the bio
	orig_private = bio->bi_private;
	orig_end_io = bio->bi_end_io;
	bio_orig_idx = bio_idx(bio);
	bio_orig_size = bio_size(bio);
	bio_orig_sect = bio_sector(bio);

	dattobd_bio_set_dev(bio, dev->sd_base_dev);
	dattobd_set_bio_ops(bio, REQ_OP_READ, READ_SYNC);

	//detect fastpath for bios completely contained within either the cow file or the base device
	ret = snap_read_bio_get_mode(dev, bio, &mode);
	if(ret) goto out;

	//submit the bio to the base device and wait for completion
	if(mode != READ_MODE_COW_FILE){
		ret = dattobd_submit_bio_wait(bio);
		if(ret){
			LOG_ERROR(ret, "error reading from base device for read");
			goto out;
		}

#ifdef HAVE_BIO_BI_REMAINING
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
		atomic_inc(&bio->bi_remaining);
#endif
	}

	if(mode != READ_MODE_BASE_DEVICE){
		//reset the bio
		bio_idx(bio) = bio_orig_idx;
		bio_size(bio) = bio_orig_size;
		bio_sector(bio) = bio_orig_sect;
		cur_sect = bio_sector(bio);

		//iterate over all the segments and fill the bio. this more complex than writing since we don't have the block aligned guarantee
		bio_for_each_segment(bvec, bio, iter){
			//map the page into kernel space
			data = kmap(bio_iter_page(bio, iter));

			cur_block = (cur_sect * SECTOR_SIZE) / COW_BLOCK_SIZE;
			block_off = (cur_sect * SECTOR_SIZE) % COW_BLOCK_SIZE;
			bvec_off = bio_iter_offset(bio, iter);

			while(bvec_off < bio_iter_offset(bio, iter) + bio_iter_len(bio, iter)){
				bytes_to_copy = min(bio_iter_offset(bio, iter) + bio_iter_len(bio, iter) - bvec_off, COW_BLOCK_SIZE - block_off);
				//check if the mapping exists
				ret = cow_read_mapping(dev->sd_cow, cur_block, &block_mapping);
				if(ret){
					kunmap(bio_iter_page(bio, iter));
					goto out;
				}

				//if the mapping exists, read it into the page, overwriting the live data
				if(block_mapping){
					ret = cow_read_data(dev->sd_cow, data + bvec_off, block_mapping, block_off, bytes_to_copy);
					if(ret){
						kunmap(bio_iter_page(bio, iter));
						goto out;
					}
				}

				cur_sect += bytes_to_copy / SECTOR_SIZE;
				cur_block = (cur_sect * SECTOR_SIZE) / COW_BLOCK_SIZE;
				block_off = (cur_sect * SECTOR_SIZE) % COW_BLOCK_SIZE;
				bvec_off += bytes_to_copy;
			}

			//unmap the page from kernel space
			kunmap(bio_iter_page(bio, iter));
		}
	}

out:
	if(ret) {
		LOG_ERROR(ret, "error handling read bio");
		bio_idx(bio) = bio_orig_idx;
		bio_size(bio) = bio_orig_size;
		bio_sector(bio) = bio_orig_sect;
	}

	//revert bio's original data
	bio->bi_private = orig_private;
	bio->bi_end_io = orig_end_io;

	return ret;
}

static int snap_handle_write_bio(const struct snap_device *dev, struct bio *bio){
	int ret;
	bio_iter_t iter;
	bio_iter_bvec_t bvec;
	char *data;
	sector_t start_block, end_block = SECTOR_TO_BLOCK(bio_sector(bio));

	//iterate through the bio and handle each segment (which is guaranteed to be block aligned)
	bio_for_each_segment(bvec, bio, iter){
		//find the start and end block
		start_block = end_block;
		end_block = start_block + (bio_iter_len(bio, iter) / COW_BLOCK_SIZE);

		//map the page into kernel space
		data = kmap(bio_iter_page(bio, iter));

		//loop through the blocks in the page
		for(; start_block < end_block; start_block++){
			//pas the block to the cow manager to be handled
			ret = cow_write_current(dev->sd_cow, start_block, data);
			if(ret){
				kunmap(bio_iter_page(bio, iter));
				goto error;
			}
		}

		//unmap the page
		kunmap(bio_iter_page(bio, iter));
	}

	return 0;

error:
	LOG_ERROR(ret, "error handling write bio");
	return ret;
}

static int inc_handle_sset(const struct snap_device *dev, struct sector_set *sset){
	int ret;
	sector_t start_block = SECTOR_TO_BLOCK(sset->sect);
	sector_t end_block = NUM_SEGMENTS(sset->sect + sset->len, COW_BLOCK_LOG_SIZE - SECTOR_SHIFT);

	for(; start_block < end_block; start_block++){
		ret = cow_write_filler_mapping(dev->sd_cow, start_block);
		if(ret) goto error;
	}

	return 0;

error:
	LOG_ERROR(ret, "error handling sset");
	return ret;
}

static int snap_mrf_thread(void *data){
	int ret;
	struct snap_device *dev = data;
	struct bio_queue *bq = &dev->sd_orig_bios;
	struct bio *bio;

	MAYBE_UNUSED(ret);

	//give this thread the highest priority we are allowed
	set_user_nice(current, MIN_NICE);

	while(!kthread_should_stop() || !bio_queue_empty(bq)) {
		//wait for a bio to process or a kthread_stop call
		wait_event_interruptible(bq->event, kthread_should_stop() || !bio_queue_empty(bq));
		if(bio_queue_empty(bq)) continue;

		//safely dequeue a bio
		bio = bio_queue_dequeue(bq);

		//submit the original bio to the block IO layer
		dattobd_bio_op_set_flag(bio, DATTOBD_PASSTHROUGH);

		ret = dattobd_call_mrf(dev->sd_orig_mrf, dattobd_bio_get_queue(bio), bio);
#ifdef HAVE_MAKE_REQUEST_FN_INT
		if(ret) generic_make_request(bio);
#endif
	}

	return 0;
}

static int snap_cow_thread(void *data){
	int ret, is_failed = 0;
	struct snap_device *dev = data;
	struct bio_queue *bq = &dev->sd_cow_bios;
	struct bio *bio;

	//give this thread the highest priority we are allowed
	set_user_nice(current, MIN_NICE);

	while(!kthread_should_stop() || !bio_queue_empty(bq) || atomic64_read(&dev->sd_submitted_cnt) != atomic64_read(&dev->sd_received_cnt)) {
		//wait for a bio to process or a kthread_stop call
		wait_event_interruptible(bq->event, kthread_should_stop() || !bio_queue_empty(bq));

		if(!is_failed && tracer_read_fail_state(dev)){
			LOG_DEBUG("error detected in cow thread, cleaning up cow");
			is_failed = 1;

			if(dev->sd_cow) cow_free_members(dev->sd_cow);
		}

		if(bio_queue_empty(bq)) continue;

		//safely dequeue a bio
		bio = bio_queue_dequeue_delay_read(bq);

		//pass bio to handler
		if(!bio_data_dir(bio)){
			//if we're in the fail state just send back an IO error and free the bio
			if(is_failed){
				dattobd_bio_endio(bio, -EIO); //end the bio with an IO error
				continue;
			}

			ret = snap_handle_read_bio(dev, bio);
			if(ret){
				LOG_ERROR(ret, "error handling read bio in kernel thread");
				tracer_set_fail_state(dev, ret);
			}

			dattobd_bio_endio(bio, (ret)? -EIO : 0);
		}else{
			if(is_failed){
				bio_free_clone(bio);
				continue;
			}

			ret = snap_handle_write_bio(dev, bio);
			if(ret){
				LOG_ERROR(ret, "error handling write bio in kernel thread");
				tracer_set_fail_state(dev, ret);
			}

			bio_free_clone(bio);
		}
	}

	return 0;
}

static int inc_sset_thread(void *data){
	int ret, is_failed = 0;
	struct snap_device *dev = data;
	struct sset_queue *sq = &dev->sd_pending_ssets;
	struct sector_set *sset;

	//give this thread the highest priority we are allowed
	set_user_nice(current, MIN_NICE);

	while(!kthread_should_stop() || !sset_queue_empty(sq)) {
		//wait for a sset to process or a kthread_stop call
		wait_event_interruptible(sq->event, kthread_should_stop() || !sset_queue_empty(sq));

		if(!is_failed && tracer_read_fail_state(dev)){
			LOG_DEBUG("error detected in sset thread, cleaning up cow");
			is_failed = 1;

			if(dev->sd_cow) cow_free_members(dev->sd_cow);
		}

		if(sset_queue_empty(sq)) continue;

		//safely dequeue a sset
		sset = sset_queue_dequeue(sq);

		//if there has been a problem don't process any more, just free the ones we have
		if(is_failed){
			kfree(sset);
			continue;
		}

		//pass the sset to the handler
		ret = inc_handle_sset(dev, sset);
		if(ret){
			LOG_ERROR(ret, "error handling sector set in kernel thread");
			tracer_set_fail_state(dev, ret);
		}

		//free the sector set
		kfree(sset);
	}

	return 0;
}

/****************************BIO TRACING LOGIC*****************************/

static void __on_bio_read_complete(struct bio *bio, int err){
	int ret;
	unsigned short i;
	struct tracing_params *tp = bio->bi_private;
	struct snap_device *dev = tp->dev;

	//check for read errors
	if(err){
		ret = err;
		LOG_ERROR(ret, "error reading from base device for copy on write");
		goto error;
	}

	//change the bio into a write bio
	dattobd_set_bio_ops(bio, REQ_OP_WRITE, 0);

	//reset the bio iterator to its original state
	for(i = 0; i < MAX_CLONES_PER_BIO && tp->bio_sects[i].bio != NULL; i++){
		if(bio == tp->bio_sects[i].bio){
			bio_sector(bio) = tp->bio_sects[i].sect - dev->sd_sect_off;
			bio_size(bio) = tp->bio_sects[i].size;
			bio_idx(bio) = 0;
			break;
		}
	}

	if(i == MAX_CLONES_PER_BIO){
		ret = -EIO;
		LOG_ERROR(ret, "clone not found in tp struct");
		goto error;
	}

	/*
	 * Reset the position in each bvec. Unnecessary with bvec iterators. Will cause multipage bvec capable kernels to
	 * lock up.
	 */
#ifndef HAVE_BVEC_ITER
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
	for(i = 0; i < bio->bi_vcnt; i++){
		bio->bi_io_vec[i].bv_len = PAGE_SIZE;
		bio->bi_io_vec[i].bv_offset = 0;
	}
#endif

	/*
	 * drop our reference to the tp (will queue the orig_bio if nobody else is using it)
	 * at this point we set bi_private to the snap_device and change the destructor to use
	 * that instead. This only matters on older kernels
	 */
	bio->bi_private = dev;
#ifndef HAVE_BIO_BI_POOL
	bio->bi_destructor = bio_destructor_snap_dev;
#endif

	//queue cow bio for processing by kernel thread
	bio_queue_add(&dev->sd_cow_bios, bio);
	atomic64_inc(&dev->sd_received_cnt);
	smp_wmb();

	tp_put(tp);

	return;

error:
	LOG_ERROR(ret, "error during bio read complete callback");
	tracer_set_fail_state(dev, ret);
	tp_put(tp);
	bio_free_clone(bio);
}

#ifdef HAVE_BIO_ENDIO_INT
static int on_bio_read_complete(struct bio *bio, unsigned int bytes, int err){
	if(bio->bi_size) return 1;
	__on_bio_read_complete(bio, err);
	return 0;
}
#elif !defined HAVE_BIO_ENDIO_1
static void on_bio_read_complete(struct bio *bio, int err){
	if(!test_bit(BIO_UPTODATE, &bio->bi_flags)) err = -EIO;
	__on_bio_read_complete(bio, err);
}
#elif defined HAVE_BLK_STATUS_T
static void on_bio_read_complete(struct bio *bio){
	__on_bio_read_complete(bio, blk_status_to_errno(bio->bi_status));
}
#else
static void on_bio_read_complete(struct bio *bio){
	__on_bio_read_complete(bio, bio->bi_error);
}
#endif

static int snap_trace_bio(struct snap_device *dev, struct bio *bio){
	int ret;
	struct bio *new_bio = NULL;
	struct tracing_params *tp = NULL;
	sector_t start_sect, end_sect;
	unsigned int bytes, pages, i = 0;

	//if we don't need to cow this bio just call the real mrf normally
	if(!bio_needs_cow(bio, dev->sd_cow_inode)) return dattobd_call_mrf(dev->sd_orig_mrf, dattobd_bio_get_queue(bio), bio);

	//the cow manager works in 4096 byte blocks, so read clones must also be 4096 byte aligned
	start_sect = ROUND_DOWN(bio_sector(bio) - dev->sd_sect_off, SECTORS_PER_BLOCK) + dev->sd_sect_off;
	end_sect = ROUND_UP(bio_sector(bio) + (bio_size(bio) / SECTOR_SIZE) - dev->sd_sect_off, SECTORS_PER_BLOCK) + dev->sd_sect_off;
	pages = (end_sect - start_sect) / SECTORS_PER_PAGE;

	//allocate tracing_params struct to hold all pointers we will need across contexts
	ret = tp_alloc(dev, bio, &tp);
	if(ret) goto error;

retry:
	//allocate and populate read bio clone. This bio may not have all the pages we need due to queue restrictions
	ret = bio_make_read_clone(dev_bioset(dev), tp, bio, start_sect, pages, &new_bio, &bytes);
	if(ret) goto error;

	//make sure we don't excede the max number of bio clones that tp can hold
	if(i >= MAX_CLONES_PER_BIO){
		ret = -EFAULT;
		goto error;
	}

	//set pointers for read clone
	tp->bio_sects[i].bio = new_bio;
	tp->bio_sects[i].sect = bio_sector(new_bio);
	tp->bio_sects[i].size = bio_size(new_bio);

	atomic64_inc(&dev->sd_submitted_cnt);
	smp_wmb();

	//submit the bios
	dattobd_submit_bio(new_bio);

	//if our bio didn't cover the entire clone we must keep creating bios until we have
	if(bytes / PAGE_SIZE < pages){
		start_sect += bytes / SECTOR_SIZE;
		pages -= bytes / PAGE_SIZE;
		i++;
		goto retry;
	}

	//drop our reference to the tp
	tp_put(tp);

	return 0;

error:
	LOG_ERROR(ret, "error tracing bio for snapshot");
	tracer_set_fail_state(dev, ret);

	//clean up the bio we allocated (but did not submit)
	if(new_bio) bio_free_clone(new_bio);
	if(tp) tp_put(tp);

	//this function only returns non-zero if the real mrf does not. Errors set the fail state.
	return 0;
}

static int inc_make_sset(struct snap_device *dev, sector_t sect, unsigned int len){
	struct sector_set *sset;

	//allocate sector set to hold record of change sectors
	sset = kmalloc(sizeof(struct sector_set), GFP_NOIO);
	if(!sset){
		LOG_ERROR(-ENOMEM, "error allocating sector set");
		return -ENOMEM;
	}

	sset->sect = sect - dev->sd_sect_off;
	sset->len = len;

	//queue sset for processing by kernel thread
	sset_queue_add(&dev->sd_pending_ssets, sset);

	return 0;
}

static int inc_trace_bio(struct snap_device *dev, struct bio *bio){
	int ret = 0, is_initialized = 0;
	sector_t start_sect = 0, end_sect = bio_sector(bio);
	bio_iter_t iter;
	bio_iter_bvec_t bvec;

#ifdef HAVE_ENUM_REQ_OPF
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	if(bio_op(bio) == REQ_OP_WRITE_ZEROES){
		ret = inc_make_sset(dev, bio_sector(bio), bio_size(bio) / SECTOR_SIZE);
		goto out;
	}
#endif

	bio_for_each_segment(bvec, bio, iter){
		if(page_get_inode(bio_iter_page(bio, iter)) != dev->sd_cow_inode){
			if(!is_initialized){
				is_initialized = 1;
				start_sect = end_sect;
			}
		}else{
			if(is_initialized && end_sect - start_sect > 0){
				ret = inc_make_sset(dev, start_sect, end_sect - start_sect);
				if(ret) goto out;
			}
			is_initialized = 0;
		}
		end_sect += (bio_iter_len(bio, iter) >> 9);
	}

	if(is_initialized && end_sect - start_sect > 0){
		ret = inc_make_sset(dev, start_sect, end_sect - start_sect);
		if(ret) goto out;
	}

out:
	if(ret){
		LOG_ERROR(ret, "error tracing bio for incremental");
		tracer_set_fail_state(dev, ret);
		ret = 0;
	}

	//call the original mrf
	ret = dattobd_call_mrf(dev->sd_orig_mrf, dattobd_bio_get_queue(bio), bio);

	return ret;
}

static MRF_RETURN_TYPE tracing_mrf(struct request_queue *q, struct bio *bio){
	int i, ret = 0;
	struct snap_device *dev;
	make_request_fn *orig_mrf = NULL;

	MAYBE_UNUSED(ret);

	smp_rmb();
	tracer_for_each(dev, i){
		if(!dev || test_bit(UNVERIFIED, &dev->sd_state) || !tracer_queue_matches_bio(dev, bio)) continue;

		orig_mrf = dev->sd_orig_mrf;
		if(dattobd_bio_op_flagged(bio, DATTOBD_PASSTHROUGH)){
			dattobd_bio_op_clear_flag(bio, DATTOBD_PASSTHROUGH);
			goto call_orig;
		}

		if(tracer_should_trace_bio(dev, bio)){
			if(test_bit(SNAPSHOT, &dev->sd_state)) ret = snap_trace_bio(dev, bio);
			else ret = inc_trace_bio(dev, bio);
			goto out;
		}
	}

call_orig:
	if(orig_mrf) ret = dattobd_call_mrf(orig_mrf, q, bio);
	else LOG_ERROR(-EFAULT, "error finding original_mrf");

out:
	MRF_RETURN(ret);
}

static MRF_RETURN_TYPE snap_mrf(struct request_queue *q, struct bio *bio){
	struct snap_device *dev = q->queuedata;

	//if a write request somehow gets sent in, discard it
	if(bio_data_dir(bio)){
		dattobd_bio_endio(bio, -EOPNOTSUPP);
		MRF_RETURN(0);
	}else if(tracer_read_fail_state(dev)){
		dattobd_bio_endio(bio, -EIO);
		MRF_RETURN(0);
	}else if(!test_bit(ACTIVE, &dev->sd_state)){
		dattobd_bio_endio(bio, -EBUSY);
		MRF_RETURN(0);
	}

	//queue bio for processing by kernel thread
	bio_queue_add(&dev->sd_cow_bios, bio);

	MRF_RETURN(0);
}

#ifdef HAVE_MERGE_BVEC_FN
#ifdef HAVE_BVEC_MERGE_DATA
static int snap_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm, struct bio_vec *bvec){
	struct snap_device *dev = q->queuedata;
	struct request_queue *base_queue = bdev_get_queue(dev->sd_base_dev);

	bvm->bi_bdev = dev->sd_base_dev;

	return base_queue->merge_bvec_fn(base_queue, bvm, bvec);
}
#else
static int snap_merge_bvec(struct request_queue *q, struct bio *bio_bvm, struct bio_vec *bvec){
	struct snap_device *dev = q->queuedata;
	struct request_queue *base_queue = bdev_get_queue(dev->sd_base_dev);

	bio_bvm->bi_bdev = dev->sd_base_dev;

	return base_queue->merge_bvec_fn(base_queue, bio_bvm, bvec);
}
#endif
#endif

/*******************************SETUP HELPER FUNCTIONS********************************/

static int bdev_is_already_traced(const struct block_device *bdev){
	int i;
	struct snap_device *dev;

	tracer_for_each(dev, i){
		if(!dev || test_bit(UNVERIFIED, &dev->sd_state)) continue;
		if(dev->sd_base_dev == bdev) return 1;
	}

	return 0;
}

static int find_orig_mrf(struct block_device *bdev, make_request_fn **mrf){
	int i;
	struct snap_device *dev;
	struct request_queue *q = bdev_get_queue(bdev);

	if(q->make_request_fn != tracing_mrf){
		*mrf = q->make_request_fn;
		return 0;
	}

	tracer_for_each(dev, i){
		if(!dev || test_bit(UNVERIFIED, &dev->sd_state)) continue;
		if(q == bdev_get_queue(dev->sd_base_dev)){
			*mrf = dev->sd_orig_mrf;
			return 0;
		}
	}

	*mrf = NULL;
	return -EFAULT;
}

static int __tracer_should_reset_mrf(const struct snap_device *dev){
	int i;
	struct snap_device *cur_dev;
	struct request_queue *q = bdev_get_queue(dev->sd_base_dev);

	if(q->make_request_fn != tracing_mrf) return 0;
	if(dev != snap_devices[dev->sd_minor]) return 0;

	//return 0 if there is another device tracing the same queue as dev.
	if(snap_devices){
		tracer_for_each(cur_dev, i){
			if(!cur_dev || test_bit(UNVERIFIED, &cur_dev->sd_state) || cur_dev == dev) continue;
			if(q == bdev_get_queue(cur_dev->sd_base_dev)) return 0;
		}
	}

	return 1;
}

static int __tracer_transition_tracing(struct snap_device *dev, struct block_device *bdev, make_request_fn *new_mrf, struct snap_device **dev_ptr){
	int ret;
	struct super_block *origsb = dattobd_get_super(bdev);
	struct super_block *sb = NULL;
	char bdev_name[BDEVNAME_SIZE];
	MAYBE_UNUSED(ret);

	bdevname(bdev, bdev_name);

	if(origsb){
		//freeze and sync block device
		LOG_DEBUG("freezing '%s'", bdev_name);
		sb = freeze_bdev(bdev);
		if(!sb){
			LOG_ERROR(-EFAULT, "error freezing '%s': null", bdev_name);
			dattobd_drop_super(origsb);
			return -EFAULT;
		}else if(IS_ERR(sb)){
			LOG_ERROR((int)PTR_ERR(sb), "error freezing '%s': error", bdev_name);
			dattobd_drop_super(origsb);
			return (int)PTR_ERR(sb);
		}
	}

	smp_wmb();
	if(dev){
		LOG_DEBUG("starting tracing");
		*dev_ptr = dev;
		smp_wmb();
		if(new_mrf) bdev->bd_disk->queue->make_request_fn = new_mrf;
	}else{
		LOG_DEBUG("ending tracing");
		if(new_mrf) bdev->bd_disk->queue->make_request_fn = new_mrf;
		smp_wmb();
		*dev_ptr = dev;
	}
	smp_wmb();

	if(origsb){
		//thaw the block device
		LOG_DEBUG("thawing '%s'", bdev_name);
#ifndef HAVE_THAW_BDEV_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
		thaw_bdev(bdev, sb);
#else
		ret = thaw_bdev(bdev, sb);
		if(ret){
			LOG_ERROR(ret, "error thawing '%s'", bdev_name);
			//we can't reasonably undo what we've done at this point, and we've replaced the mrf.
			//pretend we succeeded so we don't break the block device
		}
#endif
		dattobd_drop_super(origsb);
	}

	return 0;
}

/*******************************TRACER COMPONENT SETUP / DESTROY FUNCTIONS********************************/

static void __tracer_init(struct snap_device *dev){
	LOG_DEBUG("initializing tracer");
	atomic_set(&dev->sd_fail_code, 0);
	bio_queue_init(&dev->sd_cow_bios);
	bio_queue_init(&dev->sd_orig_bios);
	sset_queue_init(&dev->sd_pending_ssets);
}

static int tracer_alloc(struct snap_device **dev_ptr){
	int ret;
	struct snap_device *dev;

	//allocate struct
	LOG_DEBUG("allocating device struct");
	dev = kzalloc(sizeof(struct snap_device), GFP_KERNEL);
	if(!dev){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating memory for device struct");
		goto error;
	}

	__tracer_init(dev);

	*dev_ptr = dev;
	return 0;

error:
	LOG_ERROR(ret, "error allocating device struct");
	if(dev) kfree(dev);

	*dev_ptr = NULL;
	return ret;
}

static void __tracer_destroy_base_dev(struct snap_device *dev){
	dev->sd_size = 0;
	dev->sd_sect_off = 0;

	if(dev->sd_bdev_path){
		LOG_DEBUG("freeing base block device path");
		kfree(dev->sd_bdev_path);
		dev->sd_bdev_path = NULL;
	}

	if(dev->sd_base_dev){
		LOG_DEBUG("freeing base block device");
		dattobd_blkdev_put(dev->sd_base_dev);
		dev->sd_base_dev = NULL;
	}
}

static int __tracer_setup_base_dev(struct snap_device *dev, const char *bdev_path){
	int ret;

	//open the base block device
	LOG_DEBUG("finding block device");
	dev->sd_base_dev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);
	if(IS_ERR(dev->sd_base_dev)){
		ret = PTR_ERR(dev->sd_base_dev);
		dev->sd_base_dev = NULL;
		LOG_ERROR(ret, "error finding block device '%s'", bdev_path);
		goto error;
	}else if(!dev->sd_base_dev->bd_disk){
		ret = -EFAULT;
		LOG_ERROR(ret, "error finding block device gendisk");
		goto error;
	}

	//check block device is not already being traced
	LOG_DEBUG("checking block device is not already being traced");
	if(bdev_is_already_traced(dev->sd_base_dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "block device is already being traced");
		goto error;
	}

	//fetch the absolute pathname for the base device
	LOG_DEBUG("fetching the absolute pathname for the base device");
	ret = pathname_to_absolute(bdev_path, &dev->sd_bdev_path, NULL);
	if(ret) goto error;

	//check if device represents a partition, calculate size and offset
	LOG_DEBUG("calculating block device size and offset");
	if(dev->sd_base_dev->bd_contains != dev->sd_base_dev){
		dev->sd_sect_off = dev->sd_base_dev->bd_part->start_sect;
		dev->sd_size = dattobd_bdev_size(dev->sd_base_dev);
	}else{
		dev->sd_sect_off = 0;
		dev->sd_size = get_capacity(dev->sd_base_dev->bd_disk);
	}

	LOG_DEBUG("bdev size = %llu, offset = %llu", (unsigned long long)dev->sd_size, (unsigned long long)dev->sd_sect_off);

	return 0;

error:
	LOG_ERROR(ret, "error setting up base block device");
	__tracer_destroy_base_dev(dev);
	return ret;
}

static void __tracer_copy_base_dev(const struct snap_device *src, struct snap_device *dest){
	dest->sd_size = src->sd_size;
	dest->sd_sect_off = src->sd_sect_off;
	dest->sd_base_dev = src->sd_base_dev;
	dest->sd_bdev_path = src->sd_bdev_path;
}

static int __tracer_destroy_cow(struct snap_device *dev, int close_method){
	int ret = 0;

	dev->sd_cow_inode = NULL;
	dev->sd_falloc_size = 0;
	dev->sd_cache_size = 0;

	if(dev->sd_cow){
		LOG_DEBUG("destroying cow manager");

		if(close_method == 0){
			cow_free(dev->sd_cow);
			dev->sd_cow = NULL;
		}else if(close_method == 1){
			ret = cow_sync_and_free(dev->sd_cow);
			dev->sd_cow = NULL;
		}else if(close_method == 2){
			ret = cow_sync_and_close(dev->sd_cow);
			task_work_flush();
		}
	}

	return ret;
}
#define __tracer_destroy_cow_free(dev) __tracer_destroy_cow(dev, 0)
#define __tracer_destroy_cow_sync_and_free(dev) __tracer_destroy_cow(dev, 1)
#define __tracer_destroy_cow_sync_and_close(dev) __tracer_destroy_cow(dev, 2)


static int file_is_on_bdev(const struct file *file, struct block_device *bdev) {
	struct super_block *sb = dattobd_get_super(bdev);
	int ret = 0;
	if (sb) {
		ret = ((dattobd_get_mnt(file))->mnt_sb == sb);
		dattobd_drop_super(sb);
	}
	return ret;
}

static int __tracer_setup_cow(struct snap_device *dev, struct block_device *bdev, const char *cow_path, sector_t size, unsigned long fallocated_space, unsigned long cache_size, const uint8_t *uuid, uint64_t seqid, int open_method){
	int ret;
	uint64_t max_file_size;
	char bdev_name[BDEVNAME_SIZE];

	bdevname(bdev, bdev_name);

	if(open_method == 3){
		//reopen the cow manager
		LOG_DEBUG("reopening the cow manager with file '%s'", cow_path);
		ret = cow_reopen(dev->sd_cow, cow_path);
		if(ret) goto error;
	}else{
		if(!cache_size) dev->sd_cache_size = dattobd_cow_max_memory_default;
		else dev->sd_cache_size = cache_size;

		if(open_method == 0){
			//calculate how much space should be allocated to the cow file
			if(!fallocated_space){
				max_file_size = size * SECTOR_SIZE * dattobd_cow_fallocate_percentage_default;
				do_div(max_file_size, 100);
				dev->sd_falloc_size = max_file_size;
				do_div(dev->sd_falloc_size, (1024 * 1024));
			}else{
				max_file_size = fallocated_space * (1024 * 1024);
				dev->sd_falloc_size = fallocated_space;
			}

			//create and open the cow manager
			LOG_DEBUG("creating cow manager");
			ret = cow_init(cow_path, SECTOR_TO_BLOCK(size), COW_SECTION_SIZE, dev->sd_cache_size, max_file_size, uuid, seqid, &dev->sd_cow);
			if(ret) goto error;
		}else{
			//reload the cow manager
			LOG_DEBUG("reloading cow manager");
			ret = cow_reload(cow_path, SECTOR_TO_BLOCK(size), COW_SECTION_SIZE, dev->sd_cache_size, (open_method == 2), &dev->sd_cow);
			if(ret) goto error;

			dev->sd_falloc_size = dev->sd_cow->file_max;
			do_div(dev->sd_falloc_size, (1024 * 1024));
		}
	}

	//verify that file is on block device
	if(!file_is_on_bdev(dev->sd_cow->filp, bdev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "'%s' is not on '%s'", cow_path, bdev_name);
		goto error;
	}

	//find the cow file's inode number
	LOG_DEBUG("finding cow file inode");
	dev->sd_cow_inode = dattobd_get_dentry(dev->sd_cow->filp)->d_inode;

	return 0;

error:
	LOG_ERROR(ret, "error setting up cow manager");
	if(open_method != 3) __tracer_destroy_cow_free(dev);
	return ret;
}
#define __tracer_setup_cow_new(dev, bdev, cow_path, size, fallocated_space, cache_size, uuid, seqid) __tracer_setup_cow(dev, bdev, cow_path, size, fallocated_space, cache_size, uuid, seqid, 0)
#define __tracer_setup_cow_reload_snap(dev, bdev, cow_path, size, cache_size) __tracer_setup_cow(dev, bdev, cow_path, size, 0, cache_size, NULL, 0, 1)
#define __tracer_setup_cow_reload_inc(dev, bdev, cow_path, size, cache_size) __tracer_setup_cow(dev, bdev, cow_path, size, 0, cache_size, NULL, 0, 2)
#define __tracer_setup_cow_reopen(dev, bdev, cow_path) __tracer_setup_cow(dev, bdev, cow_path, 0, 0, 0, NULL, 0, 3)

static void __tracer_copy_cow(const struct snap_device *src, struct snap_device *dest){
	dest->sd_cow = src->sd_cow;
	dest->sd_cow_inode = src->sd_cow_inode;
	dest->sd_cache_size = src->sd_cache_size;
	dest->sd_falloc_size = src->sd_falloc_size;
}

static void __tracer_destroy_cow_path(struct snap_device *dev){
	if(dev->sd_cow_path){
		LOG_DEBUG("freeing cow path");
		kfree(dev->sd_cow_path);
		dev->sd_cow_path = NULL;
	}
}

static int __tracer_setup_cow_path(struct snap_device *dev, const struct file *cow_file){
	int ret;

	//get the pathname of the cow file (relative to the mountpoint)
	LOG_DEBUG("getting relative pathname of cow file");
	ret = dentry_get_relative_pathname(dattobd_get_dentry(cow_file), &dev->sd_cow_path, NULL);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error setting up cow file path");
	__tracer_destroy_cow_path(dev);
	return ret;
}

static void __tracer_copy_cow_path(const struct snap_device *src, struct snap_device *dest){
	dest->sd_cow_path = src->sd_cow_path;
}

static inline void __tracer_bioset_exit(struct snap_device *dev){
#ifndef HAVE_BIOSET_INIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
	if(dev->sd_bioset){
		LOG_DEBUG("freeing bio set");
		bioset_free(dev->sd_bioset);
		dev->sd_bioset = NULL;
	}
#else
	bioset_exit(&dev->sd_bioset);
#endif
}

static void __tracer_destroy_snap(struct snap_device *dev){
	if(dev->sd_mrf_thread){
		LOG_DEBUG("stopping mrf thread");
		kthread_stop(dev->sd_mrf_thread);
		dev->sd_mrf_thread = NULL;
	}

	if(dev->sd_gd){
		LOG_DEBUG("freeing gendisk");
		if(dev->sd_gd->flags & GENHD_FL_UP) del_gendisk(dev->sd_gd);
		put_disk(dev->sd_gd);
		dev->sd_gd = NULL;
	}

	if(dev->sd_queue){
		LOG_DEBUG("freeing request queue");
		blk_cleanup_queue(dev->sd_queue);
		dev->sd_queue = NULL;
	}

	__tracer_bioset_exit(dev);
}

static int __tracer_bioset_init(struct snap_device *dev){
#ifndef HAVE_BIOSET_INIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
	dev->sd_bioset = dattobd_bioset_create(BIO_SET_SIZE, BIO_SET_SIZE, 0);
	if(!dev->sd_bioset) return -ENOMEM;
	return 0;
#else
	return bioset_init(&dev->sd_bioset, BIO_SET_SIZE, BIO_SET_SIZE, BIOSET_NEED_BVECS);
#endif
}

static int __tracer_setup_snap(struct snap_device *dev, unsigned int minor, struct block_device *bdev, sector_t size){
	int ret;

	ret = __tracer_bioset_init(dev);
	if(ret){
		LOG_ERROR(ret, "error initializing bio set");
		goto error;
	}

	//allocate request queue
	LOG_DEBUG("allocating queue");
	dev->sd_queue = blk_alloc_queue(GFP_KERNEL);
	if(!dev->sd_queue){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating request queue");
		goto error;
	}

	//register request handler
	LOG_DEBUG("setting up make request function");
	blk_queue_make_request(dev->sd_queue, snap_mrf);

	//give our request queue the same properties as the base device's
	LOG_DEBUG("setting queue limits");
	blk_set_stacking_limits(&dev->sd_queue->limits);
	dattobd_bdev_stack_limits(dev->sd_queue, bdev, 0);

#ifdef HAVE_MERGE_BVEC_FN
	//use a thin wrapper around the base device's merge_bvec_fn
	if(bdev_get_queue(bdev)->merge_bvec_fn) blk_queue_merge_bvec(dev->sd_queue, snap_merge_bvec);
#endif

	//allocate a gendisk struct
	LOG_DEBUG("allocating gendisk");
	dev->sd_gd = alloc_disk(1);
	if(!dev->sd_gd){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating gendisk");
		goto error;
	}

	//initialize gendisk and request queue values
	LOG_DEBUG("initializing gendisk");
	dev->sd_queue->queuedata = dev;
	dev->sd_gd->private_data = dev;
	dev->sd_gd->major = major;
	dev->sd_gd->first_minor = minor;
	dev->sd_gd->fops = &snap_ops;
	dev->sd_gd->queue = dev->sd_queue;

	//name our gendisk
	LOG_DEBUG("naming gendisk");
	snprintf(dev->sd_gd->disk_name, 32, SNAP_DEVICE_NAME, minor);

	//set the capacity of our gendisk
	LOG_DEBUG("block device size: %llu", (unsigned long long)size);
	set_capacity(dev->sd_gd, size);

#ifdef HAVE_GENHD_FL_NO_PART_SCAN
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	//disable partition scanning (the device should not have any sub-partitions)
	dev->sd_gd->flags |= GENHD_FL_NO_PART_SCAN;
#endif

	//set the device as read-only
	set_disk_ro(dev->sd_gd, 1);

	//register gendisk with the kernel
	LOG_DEBUG("adding disk");
	add_disk(dev->sd_gd);

	LOG_DEBUG("starting mrf kernel thread");
	dev->sd_mrf_thread = kthread_run(snap_mrf_thread, dev, SNAP_MRF_THREAD_NAME_FMT, minor);
	if(IS_ERR(dev->sd_mrf_thread)){
		ret = PTR_ERR(dev->sd_mrf_thread);
		dev->sd_mrf_thread = NULL;
		LOG_ERROR(ret, "error starting mrf kernel thread");
		goto error;
	}

	atomic64_set(&dev->sd_submitted_cnt, 0);
	atomic64_set(&dev->sd_received_cnt, 0);

	return 0;

error:
	LOG_ERROR(ret, "error setting up snapshot");
	__tracer_destroy_snap(dev);
	return ret;
}

static void __tracer_destroy_cow_thread(struct snap_device *dev){
	if(dev->sd_cow_thread){
		LOG_DEBUG("stopping cow thread");
		kthread_stop(dev->sd_cow_thread);
		dev->sd_cow_thread = NULL;
	}
}

static int __tracer_setup_cow_thread(struct snap_device *dev, unsigned int minor, int is_snap){
	int ret;

	LOG_DEBUG("creating kernel cow thread");
	if(is_snap) dev->sd_cow_thread = kthread_create(snap_cow_thread, dev, SNAP_COW_THREAD_NAME_FMT, minor);
	else dev->sd_cow_thread = kthread_create(inc_sset_thread, dev, INC_THREAD_NAME_FMT, minor);

	if(IS_ERR(dev->sd_cow_thread)){
		ret = PTR_ERR(dev->sd_cow_thread);
		dev->sd_cow_thread = NULL;
		LOG_ERROR(ret, "error creating kernel thread");
		goto error;
	}

	return 0;

error:
	LOG_ERROR(ret, "error setting up cow thread");
	__tracer_destroy_cow_thread(dev);
	return ret;
}
#define __tracer_setup_inc_cow_thread(dev, minor)  __tracer_setup_cow_thread(dev, minor, 0)
#define __tracer_setup_snap_cow_thread(dev, minor)  __tracer_setup_cow_thread(dev, minor, 1)

static void minor_range_recalculate(void){
	unsigned int i, highest = 0, lowest = dattobd_max_snap_devices - 1;
	struct snap_device *dev;

	tracer_for_each_full(dev, i){
		if(!dev) continue;

		if(i < lowest) lowest = i;
		if(i > highest) highest = i;
	}

	lowest_minor = lowest;
	highest_minor = highest;
}

static void minor_range_include(unsigned int minor){
	if(minor < lowest_minor) lowest_minor = minor;
	if(minor > highest_minor) highest_minor = minor;
}

static void __tracer_destroy_tracing(struct snap_device *dev){
	if(dev->sd_orig_mrf){
		LOG_DEBUG("replacing make_request_fn if needed");
		if(__tracer_should_reset_mrf(dev)) __tracer_transition_tracing(NULL, dev->sd_base_dev, dev->sd_orig_mrf, &snap_devices[dev->sd_minor]);
		else __tracer_transition_tracing(NULL, dev->sd_base_dev, NULL, &snap_devices[dev->sd_minor]);

		dev->sd_orig_mrf = NULL;
	}else if(snap_devices[dev->sd_minor] == dev){
		smp_wmb();
		snap_devices[dev->sd_minor] = NULL;
		smp_wmb();
	}

	dev->sd_minor = 0;
	minor_range_recalculate();
}

static void __tracer_setup_tracing_unverified(struct snap_device *dev, unsigned int minor){
	dev->sd_orig_mrf = NULL;
	minor_range_include(minor);
	smp_wmb();
	dev->sd_minor = minor;
	snap_devices[minor] = dev;
	smp_wmb();
}


static int __tracer_setup_tracing(struct snap_device *dev, unsigned int minor){
	int ret;

	dev->sd_minor = minor;
	minor_range_include(minor);

	//get the base block device's make_request_fn
	LOG_DEBUG("getting the base block device's make_request_fn");
	ret = find_orig_mrf(dev->sd_base_dev, &dev->sd_orig_mrf);
	if(ret) goto error;

	ret = __tracer_transition_tracing(dev, dev->sd_base_dev, tracing_mrf, &snap_devices[minor]);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error setting up tracing");
	dev->sd_minor = 0;
	dev->sd_orig_mrf = NULL;
	minor_range_recalculate();
	return ret;
}

/************************SETUP / DESTROY FUNCTIONS************************/

static void tracer_destroy(struct snap_device *dev){
	__tracer_destroy_tracing(dev);
	__tracer_destroy_cow_thread(dev);
	__tracer_destroy_snap(dev);
	__tracer_destroy_cow_path(dev);
	__tracer_destroy_cow_free(dev);
	__tracer_destroy_base_dev(dev);
}

static int tracer_setup_active_snap(struct snap_device *dev, unsigned int minor, const char *bdev_path, const char *cow_path, unsigned long fallocated_space, unsigned long cache_size){
	int ret;

	set_bit(SNAPSHOT, &dev->sd_state);
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);

	//setup base device
	ret = __tracer_setup_base_dev(dev, bdev_path);
	if(ret) goto error;

	//setup the cow manager
	ret = __tracer_setup_cow_new(dev, dev->sd_base_dev, cow_path, dev->sd_size, fallocated_space, cache_size, NULL, 1);
	if(ret) goto error;

	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret) goto error;

	//setup the snapshot values
	ret = __tracer_setup_snap(dev, minor, dev->sd_base_dev, dev->sd_size);
	if(ret) goto error;

	//setup the cow thread and run it
	ret = __tracer_setup_snap_cow_thread(dev, minor);
	if(ret) goto error;

	wake_up_process(dev->sd_cow_thread);

	//inject the tracing function
	ret = __tracer_setup_tracing(dev, minor);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error setting up tracer as active snapshot");
	tracer_destroy(dev);
	return ret;
}

static int __tracer_setup_unverified(struct snap_device *dev, unsigned int minor, const char *bdev_path, const char *cow_path, unsigned long cache_size, int is_snap){
	if(is_snap) set_bit(SNAPSHOT, &dev->sd_state);
	else clear_bit(SNAPSHOT, &dev->sd_state);
	clear_bit(ACTIVE, &dev->sd_state);
	set_bit(UNVERIFIED, &dev->sd_state);

	dev->sd_cache_size = cache_size;

	dev->sd_bdev_path = kstrdup(bdev_path, GFP_KERNEL);
	if(!dev->sd_bdev_path) goto error;

	dev->sd_cow_path = kstrdup(cow_path, GFP_KERNEL);
	if(!dev->sd_cow_path) goto error;

	//add the tracer to the array of devices
	__tracer_setup_tracing_unverified(dev, minor);

	return 0;

error:
	LOG_ERROR(-ENOMEM, "error setting up unverified tracer");
	tracer_destroy(dev);
	return -ENOMEM;
}
#define tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path, cache_size) __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, 0)
#define tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path, cache_size) __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, 1)

/************************IOCTL TRANSITION FUNCTIONS************************/

static int tracer_active_snap_to_inc(struct snap_device *old_dev){
	int ret;
	struct snap_device *dev;
	char *abs_path = NULL;
	int abs_path_len;

	//allocate new tracer
	ret = tracer_alloc(&dev);
	if(ret) return ret;

	clear_bit(SNAPSHOT, &dev->sd_state);
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);

	//copy / set fields we need
	__tracer_copy_base_dev(old_dev, dev);
	__tracer_copy_cow_path(old_dev, dev);

	//copy cow manager to new device. Care must be taken to make sure it isn't used by multiple threads at once.
	__tracer_copy_cow(old_dev, dev);

	//setup the cow thread
	ret = __tracer_setup_inc_cow_thread(dev, old_dev->sd_minor);
	if(ret) goto error;

	//inject the tracing function
	ret = __tracer_setup_tracing(dev, old_dev->sd_minor);
	if(ret) goto error;

	//Below this point, we are commited to the new device, so we must make sure it is in a good state.

	//stop the old cow thread. Must be done before starting the new cow thread to prevent concurrent access.
	__tracer_destroy_cow_thread(old_dev);

	//sanity check to ensure no errors have occurred while cleaning up the old cow thread
	ret = tracer_read_fail_state(old_dev);
	if(ret){
		LOG_ERROR(ret, "errors occurred while cleaning up cow thread, putting incremental into error state");
		tracer_set_fail_state(dev, ret);

		//must make up the new thread regardless of errors so that any queued ssets are cleaned up
		wake_up_process(dev->sd_cow_thread);

		//clean up the old device no matter what
		__tracer_destroy_snap(old_dev);
		kfree(old_dev);

		return ret;
	}

	//wake up new cow thread. Must happen regardless of errors syncing the old cow thread in order to ensure no IO's are leaked.
	wake_up_process(dev->sd_cow_thread);

	//truncate the cow file
	ret = cow_truncate_to_index(dev->sd_cow);
	if(ret){
		//not a critical error, we can just print a warning
		file_get_absolute_pathname(dev->sd_cow->filp, &abs_path, &abs_path_len);
		if(!abs_path){
			LOG_WARN("warning: cow file truncation failed, incremental will use more disk space than needed");
		}else{
			LOG_WARN("warning: failed to truncate '%s', incremental will use more disk space than needed", abs_path);
			kfree(abs_path);
		}
	}

	//destroy the unneeded fields of the old_dev and the old_dev itself
	__tracer_destroy_snap(old_dev);
	kfree(old_dev);

	return 0;

error:
	LOG_ERROR(ret, "error transitioning to incremental mode");
	__tracer_destroy_cow_thread(dev);
	kfree(dev);

	return ret;
}

static int tracer_active_inc_to_snap(struct snap_device *old_dev, const char *cow_path, unsigned long fallocated_space){
	int ret;
	struct snap_device *dev;

	//allocate new tracer
	ret = tracer_alloc(&dev);
	if(ret) return ret;

	set_bit(SNAPSHOT, &dev->sd_state);
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);

	fallocated_space = (fallocated_space)? fallocated_space : old_dev->sd_falloc_size;

	//copy / set fields we need
	__tracer_copy_base_dev(old_dev, dev);

	//setup the cow manager
	ret = __tracer_setup_cow_new(dev, dev->sd_base_dev, cow_path, dev->sd_size, fallocated_space, dev->sd_cache_size, old_dev->sd_cow->uuid, old_dev->sd_cow->seqid + 1);
	if(ret) goto error;

	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret) goto error;

	//setup the snapshot values
	ret = __tracer_setup_snap(dev, old_dev->sd_minor, dev->sd_base_dev, dev->sd_size);
	if(ret) goto error;

	//setup the cow thread
	ret = __tracer_setup_snap_cow_thread(dev, old_dev->sd_minor);
	if(ret) goto error;

	//start tracing (overwrites old_dev's tracing)
	ret = __tracer_setup_tracing(dev, old_dev->sd_minor);
	if(ret) goto error;

	//stop the old cow thread and start the new one
	__tracer_destroy_cow_thread(old_dev);
	wake_up_process(dev->sd_cow_thread);

	//destroy the unneeded fields of the old_dev and the old_dev itself
	__tracer_destroy_cow_path(old_dev);
	__tracer_destroy_cow_sync_and_free(old_dev);
	kfree(old_dev);

	return 0;

error:
	LOG_ERROR(ret, "error transitioning tracer to snapshot mode");
	__tracer_destroy_cow_thread(dev);
	__tracer_destroy_snap(dev);
	__tracer_destroy_cow_path(dev);
	__tracer_destroy_cow_free(dev);
	kfree(dev);

	return ret;
}

static void tracer_reconfigure(struct snap_device *dev, unsigned long cache_size){
	dev->sd_cache_size = cache_size;
	if(!cache_size) cache_size = dattobd_cow_max_memory_default;
	if(test_bit(ACTIVE, &dev->sd_state)) cow_modify_cache_size(dev->sd_cow, cache_size);
}

static void tracer_dattobd_info(const struct snap_device *dev, struct dattobd_info *info){
	info->minor = dev->sd_minor;
	info->state = dev->sd_state;
	info->error = tracer_read_fail_state(dev);
	info->cache_size = (dev->sd_cache_size)? dev->sd_cache_size : dattobd_cow_max_memory_default;
	strlcpy(info->cow, dev->sd_cow_path, PATH_MAX);
	strlcpy(info->bdev, dev->sd_bdev_path, PATH_MAX);

	if(!test_bit(UNVERIFIED, &dev->sd_state)){
		info->falloc_size = dev->sd_cow->file_max;
		info->seqid = dev->sd_cow->seqid;
		memcpy(info->uuid, dev->sd_cow->uuid, COW_UUID_SIZE);
		info->version = dev->sd_cow->version;
		info->nr_changed_blocks = dev->sd_cow->nr_changed_blocks;
	}else{
		info->falloc_size = 0;
		info->seqid = 0;
		memset(info->uuid, 0, COW_UUID_SIZE);
	}
}

/************************IOCTL HANDLER FUNCTIONS************************/

static int __verify_minor(unsigned int minor, int mode){
	//check minor number is within range
	if(minor >= dattobd_max_snap_devices){
		LOG_ERROR(-EINVAL, "minor number specified is out of range");
		return -EINVAL;
	}

	//check if the device is in use
	if(mode == 0){
		if(snap_devices[minor]){
			LOG_ERROR(-EBUSY, "device specified already exists");
			return -EBUSY;
		}
	}else{
		if(!snap_devices[minor]){
			LOG_ERROR(-ENOENT, "device specified does not exist");
			return -ENOENT;
		}

		//check that the device is not busy if we care
		if(mode == 1 && atomic_read(&snap_devices[minor]->sd_refs)){
			LOG_ERROR(-EBUSY, "device specified is busy");
			return -EBUSY;
		}
	}

	return 0;
}
#define verify_minor_available(minor) __verify_minor(minor, 0)
#define verify_minor_in_use_not_busy(minor) __verify_minor(minor, 1)
#define verify_minor_in_use(minor) __verify_minor(minor, 2)

static int __verify_bdev_writable(const char *bdev_path, int *out){
	int writable = 0;
	struct block_device *bdev;
	struct super_block *sb;

	//open the base block device
	bdev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);

	if(IS_ERR(bdev)){
		*out = 0;
		return PTR_ERR(bdev);
	}

	sb = dattobd_get_super(bdev);
	if(sb){
		writable = !(sb->s_flags & MS_RDONLY);
		dattobd_drop_super(sb);
	}

	dattobd_blkdev_put(bdev);
	*out = writable;
	return 0;
}

static int __ioctl_setup(unsigned int minor, const char *bdev_path, const char *cow_path, unsigned long fallocated_space, unsigned long cache_size, int is_snap, int is_reload){
	int ret, is_mounted;
	struct snap_device *dev = NULL;

	LOG_DEBUG("received %s %s ioctl - %u : %s : %s", (is_reload)? "reload" : "setup", (is_snap)? "snap" : "inc", minor, bdev_path, cow_path);

	//verify that the minor number is valid
	ret = verify_minor_available(minor);
	if(ret) goto error;

	//check if block device is mounted
	ret = __verify_bdev_writable(bdev_path, &is_mounted);
	if(ret) goto error;

	//check that reload / setup command matches current mount state
	if(is_mounted && is_reload){
		ret = -EINVAL;
		LOG_ERROR(ret, "illegal to perform reload while mounted");
		goto error;
	}else if(!is_mounted && !is_reload){
		ret = -EINVAL;
		LOG_ERROR(ret, "illegal to perform setup while unmounted");
		goto error;
	}

	//allocate the tracing struct
	ret = tracer_alloc(&dev);
	if(ret) goto error;

	//route to the appropriate setup function
	if(is_snap){
		if(is_mounted) ret = tracer_setup_active_snap(dev, minor, bdev_path, cow_path, fallocated_space, cache_size);
		else ret = tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path, cache_size);
	}else{
		if(!is_mounted) ret = tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path, cache_size);
		else{
			ret = -EINVAL;
			LOG_ERROR(ret, "illegal to setup as active incremental");
			goto error;
		}
	}

	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error during setup ioctl handler");
	if(dev) kfree(dev);
	return ret;
}
#define ioctl_setup_snap(minor, bdev_path, cow_path, fallocated_space, cache_size) __ioctl_setup(minor, bdev_path, cow_path, fallocated_space, cache_size, 1, 0)
#define ioctl_reload_snap(minor, bdev_path, cow_path, cache_size) __ioctl_setup(minor, bdev_path, cow_path, 0, cache_size, 1, 1)
#define ioctl_reload_inc(minor, bdev_path, cow_path, cache_size) __ioctl_setup(minor, bdev_path, cow_path, 0, cache_size, 0, 1)

static int ioctl_destroy(unsigned int minor){
	int ret;
	struct snap_device *dev;

	LOG_DEBUG("received destroy ioctl - %u", minor);

	//verify that the minor number is valid
	ret = verify_minor_in_use_not_busy(minor);
	if(ret){
		LOG_ERROR(ret, "error during destroy ioctl handler");
		return ret;
	}

	dev = snap_devices[minor];
	tracer_destroy(snap_devices[minor]);
	kfree(dev);

	return 0;
}

static int ioctl_transition_inc(unsigned int minor){
	int ret;
	struct snap_device *dev;

	LOG_DEBUG("received transition inc ioctl - %u", minor);

	//verify that the minor number is valid
	ret = verify_minor_in_use_not_busy(minor);
	if(ret) goto error;

	dev = snap_devices[minor];

	//check that the device is not in the fail state
	if(tracer_read_fail_state(dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is in the fail state");
		goto error;
	}

	//check that tracer is in active snapshot state
	if(!test_bit(SNAPSHOT, &dev->sd_state) || !test_bit(ACTIVE, &dev->sd_state)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is not in active snapshot mode");
		goto error;
	}

	ret = tracer_active_snap_to_inc(dev);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error during transition to incremental ioctl handler");
	return ret;
}

static int ioctl_transition_snap(unsigned int minor, const char *cow_path, unsigned long fallocated_space){
	int ret;
	struct snap_device *dev;

	LOG_DEBUG("received transition snap ioctl - %u : %s", minor, cow_path);

	//verify that the minor number is valid
	ret = verify_minor_in_use_not_busy(minor);
	if(ret) goto error;

	dev = snap_devices[minor];

	//check that the device is not in the fail state
	if(tracer_read_fail_state(dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is in the fail state");
		goto error;
	}

	//check that tracer is in active incremental state
	if(test_bit(SNAPSHOT, &dev->sd_state) || !test_bit(ACTIVE, &dev->sd_state)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is not in active incremental mode");
		goto error;
	}

	ret = tracer_active_inc_to_snap(dev, cow_path, fallocated_space);
	if(ret) goto error;

	return 0;

error:
	LOG_ERROR(ret, "error during transition to snapshot ioctl handler");
	return ret;
}

static int ioctl_reconfigure(unsigned int minor, unsigned long cache_size){
	int ret;
	struct snap_device *dev;

	LOG_DEBUG("received reconfigure ioctl - %u : %lu", minor, cache_size);

	//verify that the minor number is valid
	ret = verify_minor_in_use_not_busy(minor);
	if(ret) goto error;

	dev = snap_devices[minor];

	//check that the device is not in the fail state
	if(tracer_read_fail_state(dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is in the fail state");
		goto error;
	}

	tracer_reconfigure(dev, cache_size);

	return 0;

error:
	LOG_ERROR(ret, "error during reconfigure ioctl handler");
	return ret;
}

static int ioctl_dattobd_info(struct dattobd_info *info){
	int ret;
	struct snap_device *dev;

	LOG_DEBUG("received dattobd info ioctl - %u", info->minor);

	//verify that the minor number is valid
	ret = verify_minor_in_use(info->minor);
	if(ret) goto error;

	dev = snap_devices[info->minor];

	tracer_dattobd_info(dev, info);

	return 0;

error:
	LOG_ERROR(ret, "error during reconfigure ioctl handler");
	return ret;
}

static int get_free_minor(void)
{
	struct snap_device *dev;
	int i;

	tracer_for_each_full(dev, i){
		if(!dev) return i;
	}

	return -ENOENT;
}

static long ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	int ret, idx;
	char *bdev_path = NULL;
	char *cow_path = NULL;
	struct dattobd_info *info = NULL;
	unsigned int minor = 0;
	unsigned long fallocated_space = 0, cache_size = 0;

	LOG_DEBUG("ioctl command received: %d", cmd);
	mutex_lock(&ioctl_mutex);

	switch(cmd){
	case IOCTL_SETUP_SNAP:
		//get params from user space
		ret = get_setup_params((struct setup_params __user *)arg, &minor, &bdev_path, &cow_path, &fallocated_space, &cache_size);
		if(ret) break;

		ret = ioctl_setup_snap(minor, bdev_path, cow_path, fallocated_space, cache_size);
		if(ret) break;

		break;
	case IOCTL_RELOAD_SNAP:
		//get params from user space
		ret = get_reload_params((struct reload_params __user *)arg, &minor, &bdev_path, &cow_path, &cache_size);
		if(ret) break;

		ret = ioctl_reload_snap(minor, bdev_path, cow_path, cache_size);
		if(ret) break;

		break;
	case IOCTL_RELOAD_INC:
		//get params from user space
		ret = get_reload_params((struct reload_params __user *)arg, &minor, &bdev_path, &cow_path, &cache_size);
		if(ret) break;

		ret = ioctl_reload_inc(minor, bdev_path, cow_path, cache_size);
		if(ret) break;

		break;
	case IOCTL_DESTROY:
		//get minor from user space
		ret = get_user(minor, (unsigned int __user *)arg);
		if(ret){
			LOG_ERROR(ret, "error copying minor number from user space");
			break;
		}

		ret = ioctl_destroy(minor);
		if(ret) break;

		break;
	case IOCTL_TRANSITION_INC:
		//get minor from user space
		ret = get_user(minor, (unsigned int __user *)arg);
		if(ret){
			LOG_ERROR(ret, "error copying minor number from user space");
			break;
		}

		ret = ioctl_transition_inc(minor);
		if(ret) break;

		break;
	case IOCTL_TRANSITION_SNAP:
		//get params from user space
		ret = get_transition_snap_params((struct transition_snap_params __user *)arg, &minor, &cow_path, &fallocated_space);
		if(ret) break;

		ret = ioctl_transition_snap(minor, cow_path, fallocated_space);
		if(ret) break;

		break;
	case IOCTL_RECONFIGURE:
		//get params from user space
		ret = get_reconfigure_params((struct reconfigure_params __user *)arg, &minor, &cache_size);
		if(ret) break;

		ret = ioctl_reconfigure(minor, cache_size);
		if(ret) break;

		break;
	case IOCTL_DATTOBD_INFO:
		//get params from user space
		info = kmalloc(sizeof(struct dattobd_info), GFP_KERNEL);
		if(!info){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating memory for dattobd info");
			break;
		}

		ret = copy_from_user(info, (struct dattobd_info __user *)arg, sizeof(struct dattobd_info));
		if(ret){
			ret = -EFAULT;
			LOG_ERROR(ret, "error copying dattobd info struct from user space");
			break;
		}

		ret = ioctl_dattobd_info(info);
		if(ret) break;

		ret = copy_to_user((struct dattobd_info __user *)arg, info, sizeof(struct dattobd_info));
		if(ret){
			ret = -EFAULT;
			LOG_ERROR(ret, "error copying dattobd info struct to user space");
			break;
		}

		break;
	case IOCTL_GET_FREE:
		idx = get_free_minor();
		if(idx < 0){
			ret = idx;
			LOG_ERROR(ret, "no free devices");
			break;
		}

		ret = copy_to_user((int __user *)arg, &idx, sizeof(idx));
		if(ret){
			ret = -EFAULT;
			LOG_ERROR(ret, "error copying minor to user space");
			break;
		}

		break;
	default:
		ret = -EINVAL;
		LOG_ERROR(ret, "invalid ioctl called");
		break;
	}

	LOG_DEBUG("minor range = %u - %u", lowest_minor, highest_minor);
	mutex_unlock(&ioctl_mutex);

	if(bdev_path) kfree(bdev_path);
	if(cow_path) kfree(cow_path);
	if(info) kfree(info);

	return ret;
}

/************************AUTOMATIC TRANSITION FUNCTIONS************************/

static void __tracer_active_to_dormant(struct snap_device *dev){
	int ret;

	//stop the cow thread
	__tracer_destroy_cow_thread(dev);

	//close the cow manager
	ret = __tracer_destroy_cow_sync_and_close(dev);
	if(ret) goto error;

	//mark as dormant
	smp_wmb();
	clear_bit(ACTIVE, &dev->sd_state);

	return;

error:
	LOG_ERROR(ret, "error transitioning tracer to dormant state");
	tracer_set_fail_state(dev, ret);
}

static void __tracer_unverified_snap_to_active(struct snap_device *dev, const char __user *user_mount_path){
	int ret;
	unsigned int minor = dev->sd_minor;
	char *cow_path, *bdev_path = dev->sd_bdev_path, *rel_path = dev->sd_cow_path;
	unsigned long cache_size = dev->sd_cache_size;

	//remove tracing while we setup the struct
	__tracer_destroy_tracing(dev);

	//mark as active
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);

	dev->sd_bdev_path = NULL;
	dev->sd_cow_path = NULL;

	//setup base device
	ret = __tracer_setup_base_dev(dev, bdev_path);
	if(ret) goto error;

	//generate the full pathname
	ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
	if(ret) goto error;

	//setup the cow manager
	ret = __tracer_setup_cow_reload_snap(dev, dev->sd_base_dev, cow_path, dev->sd_size, dev->sd_cache_size);
	if(ret) goto error;

	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret) goto error;

	//setup the snapshot values
	ret = __tracer_setup_snap(dev, minor, dev->sd_base_dev, dev->sd_size);
	if(ret) goto error;

	//setup the cow thread and run it
	ret = __tracer_setup_snap_cow_thread(dev, minor);
	if(ret) goto error;

	wake_up_process(dev->sd_cow_thread);

	//inject the tracing function
	ret = __tracer_setup_tracing(dev, minor);
	if(ret) goto error;

	kfree(bdev_path);
	kfree(rel_path);
	kfree(cow_path);

	return;

error:
	LOG_ERROR(ret, "error transitioning snapshot tracer to active state");
	tracer_destroy(dev);
	tracer_setup_unverified_snap(dev, minor, bdev_path, rel_path, cache_size);
	tracer_set_fail_state(dev, ret);
	kfree(bdev_path);
	kfree(rel_path);
	if(cow_path) kfree(cow_path);
}

static void __tracer_unverified_inc_to_active(struct snap_device *dev, const char __user *user_mount_path){
	int ret;
	unsigned int minor = dev->sd_minor;
	char *cow_path, *bdev_path = dev->sd_bdev_path, *rel_path = dev->sd_cow_path;
	unsigned long cache_size = dev->sd_cache_size;

	//remove tracing while we setup the struct
	__tracer_destroy_tracing(dev);

	//mark as active
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);

	dev->sd_bdev_path = NULL;
	dev->sd_cow_path = NULL;

	//setup base device
	ret = __tracer_setup_base_dev(dev, bdev_path);
	if(ret) goto error;

	//generate the full pathname
	ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
	if(ret) goto error;

	//setup the cow manager
	ret = __tracer_setup_cow_reload_inc(dev, dev->sd_base_dev, cow_path, dev->sd_size, dev->sd_cache_size);
	if(ret) goto error;

	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret) goto error;

	//setup the cow thread and run it
	ret = __tracer_setup_inc_cow_thread(dev, minor);
	if(ret) goto error;

	wake_up_process(dev->sd_cow_thread);

	//inject the tracing function
	ret = __tracer_setup_tracing(dev, minor);
	if(ret) goto error;

	kfree(bdev_path);
	kfree(rel_path);
	kfree(cow_path);

	return;

error:
	LOG_ERROR(ret, "error transitioning incremental to active state");
	tracer_destroy(dev);
	tracer_setup_unverified_inc(dev, minor, bdev_path, rel_path, cache_size);
	tracer_set_fail_state(dev, ret);
	kfree(bdev_path);
	kfree(rel_path);
	if(cow_path) kfree(cow_path);
}

static void __tracer_dormant_to_active(struct snap_device *dev, const char __user *user_mount_path){
	int ret;
	char *cow_path;

	//generate the full pathname
	ret = user_mount_pathname_concat(user_mount_path, dev->sd_cow_path, &cow_path);
	if(ret) goto error;

	//setup the cow manager
	ret = __tracer_setup_cow_reopen(dev, dev->sd_base_dev, cow_path);
	if(ret) goto error;

	//restart the cow thread
	if(test_bit(SNAPSHOT, &dev->sd_state)) ret = __tracer_setup_snap_cow_thread(dev, dev->sd_minor);
	else ret = __tracer_setup_inc_cow_thread(dev, dev->sd_minor);

	if(ret) goto error;

	wake_up_process(dev->sd_cow_thread);

	//set the state to active
	smp_wmb();
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);

	kfree(cow_path);

	return;

error:
	LOG_ERROR(ret, "error transitioning tracer to active state");
	if(cow_path) kfree(cow_path);
	tracer_set_fail_state(dev, ret);
}

/************************AUTOMATIC HANDLER FUNCTIONS************************/

static void auto_transition_dormant(unsigned int i){
	mutex_lock(&ioctl_mutex);
	__tracer_active_to_dormant(snap_devices[i]);
	mutex_unlock(&ioctl_mutex);
}

static void auto_transition_active(unsigned int i, const char __user *dir_name){
	struct snap_device *dev = snap_devices[i];

	mutex_lock(&ioctl_mutex);

	if(test_bit(UNVERIFIED, &dev->sd_state)){
		if(test_bit(SNAPSHOT, &dev->sd_state)) __tracer_unverified_snap_to_active(dev, dir_name);
		else __tracer_unverified_inc_to_active(dev, dir_name);
	}else __tracer_dormant_to_active(dev, dir_name);

	mutex_unlock(&ioctl_mutex);
}

/***************************SYSTEM CALL HOOKING***************************/

static int __handle_bdev_mount_nowrite(const struct vfsmount *mnt, unsigned int *idx_out){
	int ret;
	unsigned int i;
	struct snap_device *dev;

	tracer_for_each(dev, i){
		if(!dev || !test_bit(ACTIVE, &dev->sd_state) || tracer_read_fail_state(dev) || dev->sd_base_dev != mnt->mnt_sb->s_bdev) continue;

		//if we are unmounting the vfsmount we are using go to dormant state
		if(mnt == dattobd_get_mnt(dev->sd_cow->filp)){
			LOG_DEBUG("block device umount detected for device %d", i);
			auto_transition_dormant(i);

			ret = 0;
			goto out;
		}
	}
	i = 0;
	ret = -ENODEV;

out:
	*idx_out = i;
	return ret;
}

static int __handle_bdev_mount_writable(const char __user *dir_name, const struct block_device *bdev, unsigned int *idx_out){
	int ret;
	unsigned int i;
	struct snap_device *dev;
	struct block_device *cur_bdev;

	tracer_for_each(dev, i){
		if(!dev || test_bit(ACTIVE, &dev->sd_state) || tracer_read_fail_state(dev)) continue;

		if(test_bit(UNVERIFIED, &dev->sd_state)){
			//get the block device for the unverified tracer we are looking into
			cur_bdev = blkdev_get_by_path(dev->sd_bdev_path, FMODE_READ, NULL);
			if(IS_ERR(cur_bdev)){
				cur_bdev = NULL;
				continue;
			}

			//if the tracer's block device exists and matches the one being mounted perform transition
			if(cur_bdev == bdev){
				LOG_DEBUG("block device mount detected for unverified device %d", i);
				auto_transition_active(i, dir_name);
				dattobd_blkdev_put(cur_bdev);

				ret = 0;
				goto out;
			}

			//put the block device
			dattobd_blkdev_put(cur_bdev);

		}else if(dev->sd_base_dev == bdev){
			LOG_DEBUG("block device mount detected for dormant device %d", i);
			auto_transition_active(i, dir_name);

			ret = 0;
			goto out;
		}
	}
	i = 0;
	ret = -ENODEV;

out:
	*idx_out = i;
	return ret;
}

static int handle_bdev_mount_event(const char __user *dir_name, int follow_flags, unsigned int *idx_out, int mount_writable){
	int ret, lookup_flags = 0;
	char *pathname = NULL;
	struct path path = {};
	struct block_device *bdev;

	if(!(follow_flags & UMOUNT_NOFOLLOW)) lookup_flags |= LOOKUP_FOLLOW;

	ret = user_path_at(AT_FDCWD, dir_name, lookup_flags, &path);
	if(ret){
		//error finding path
		goto out;
	}else if(path.dentry != path.mnt->mnt_root){
		//path specified isn't a mount point
		ret = -ENODEV;
		goto out;
	}

	bdev = path.mnt->mnt_sb->s_bdev;
	if(!bdev){
		//path specified isn't mounted on a block device
		ret = -ENODEV;
		goto out;
	}

	if(!mount_writable) ret = __handle_bdev_mount_nowrite(path.mnt, idx_out);
	else ret = __handle_bdev_mount_writable(dir_name, bdev, idx_out);
	if(ret){
		//no block device found that matched an incremental
		goto out;
	}

	kfree(pathname);
	path_put(&path);
	return 0;

out:
	if(pathname) kfree(pathname);
	path_put(&path);

	*idx_out = 0;
	return ret;
}
#define handle_bdev_mount_nowrite(dir_name, follow_flags, idx_out) handle_bdev_mount_event(dir_name, follow_flags, idx_out, 0)
#define handle_bdev_mounted_writable(dir_name, idx_out) handle_bdev_mount_event(dir_name, 0, idx_out, 1)

static void post_umount_check(int dormant_ret, long umount_ret, unsigned int idx, const char __user *dir_name){
	struct block_device *bdev;
	struct snap_device *dev;
	struct super_block *sb;

	//if we didn't do anything or failed, just return
	if(dormant_ret) return;

	dev = snap_devices[idx];

	//if we successfully went dormant, but the umount call failed, reactivate
	if(umount_ret){
		bdev = blkdev_get_by_path(dev->sd_bdev_path, FMODE_READ, NULL);
		if(!bdev || IS_ERR(bdev)){
			LOG_DEBUG("device gone, moving to error state");
			tracer_set_fail_state(dev, -ENODEV);
			return;
		}

		dattobd_blkdev_put(bdev);

		LOG_DEBUG("umount call failed, reactivating tracer %u", idx);
		auto_transition_active(idx, dir_name);
		return;
	}

	//force the umount operation to complete synchronously
	task_work_flush();

	//if we went dormant, but the block device is still mounted somewhere, goto fail state
	sb = dattobd_get_super(dev->sd_base_dev);
	if(sb){
		if(!(sb->s_flags & MS_RDONLY)){
			LOG_ERROR(-EIO, "device still mounted after umounting cow file's file-system. entering error state");
			tracer_set_fail_state(dev, -EIO);
			dattobd_drop_super(sb);
			return;
		}
		dattobd_drop_super(sb);
	}

	LOG_DEBUG("post umount check succeeded");
}

static asmlinkage long mount_hook(char __user *dev_name, char __user *dir_name, char __user *type, unsigned long flags, void __user *data){
	int ret;
	int ret_dev;
	int ret_dir;
	long sys_ret;
	unsigned int idx;
	unsigned long real_flags = flags;

	//get rid of the magic value if its present
	if((real_flags & MS_MGC_MSK) == MS_MGC_VAL) real_flags &= ~MS_MGC_MSK;
	char buff_dev_name[256],buff_dir_name[256];
	ret_dev=copy_from_user(buff_dev_name,dev_name,256);
	ret_dir=copy_from_user(buff_dir_name,dir_name,256);
	if(0!=ret_dev || 0!=ret_dir)
		LOG_DEBUG("detected block device Get mount params error!");
	else
	LOG_DEBUG("detected block device mount: %s -> %s : 0x%x", buff_dev_name,
			buff_dir_name, real_flags);
	if(real_flags & (MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE | MS_MOVE) || ((real_flags & MS_RDONLY) && !(real_flags & MS_REMOUNT))){
		//bind, shared, move, or new read-only mounts it do not affect the state of the driver
		sys_ret = orig_mount(dev_name, dir_name, type, flags, data);
	}else if((real_flags & MS_RDONLY) && (real_flags & MS_REMOUNT)){
		//we are remounting read-only, same as umounting as far as the driver is concerned
		ret = handle_bdev_mount_nowrite(dir_name, 0, &idx);
		sys_ret = orig_mount(dev_name, dir_name, type, flags, data);
		post_umount_check(ret, sys_ret, idx, dir_name);
	}else{
		//new read-write mount
		sys_ret = orig_mount(dev_name, dir_name, type, flags, data);
		if(!sys_ret) handle_bdev_mounted_writable(dir_name, &idx);
	}

	LOG_DEBUG("mount returned: %ld", sys_ret);

	return sys_ret;
}

static asmlinkage long umount_hook(char __user *name, int flags){
	int ret;
	long sys_ret;
	unsigned int idx;
	char buff_dev_name[256];
	ret=copy_from_user(buff_dev_name,name,256);
	if(0!=ret)
	    LOG_DEBUG("detected block device umount error:%d", ret);
	else
	LOG_DEBUG("detected block device umount: %s : %d", buff_dev_name, flags);
	ret = handle_bdev_mount_nowrite(name, flags, &idx);
	sys_ret = orig_umount(name, flags);
	post_umount_check(ret, sys_ret, idx, name);

	LOG_DEBUG("umount returned: %ld", sys_ret);

	return sys_ret;
}

#ifdef HAVE_SYS_OLDUMOUNT
static asmlinkage long oldumount_hook(char __user *name){
	int ret;
	long sys_ret;
	unsigned int idx;

	LOG_DEBUG("detected block device oldumount: %s", name);

	ret = handle_bdev_mount_nowrite(name, 0, &idx);
	sys_ret = orig_oldumount(name);
	post_umount_check(ret, sys_ret, idx, name);

	LOG_DEBUG("oldumount returned: %ld", sys_ret);

	return sys_ret;
}
#endif

//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#ifndef X86_CR0_WP
#define X86_CR0_WP (1UL << 16)
#endif

static inline void disable_page_protection(unsigned long *cr0) {
	*cr0 = read_cr0();
	write_cr0(*cr0 & ~X86_CR0_WP);
}

static inline void reenable_page_protection(unsigned long *cr0) {
	write_cr0(*cr0);
}

static void **find_sys_call_table(void){
	long long offset;
	void **sct;

	if(!SYS_CALL_TABLE_ADDR || !SYS_MOUNT_ADDR || !SYS_UMOUNT_ADDR) return NULL;

	offset = ((void *)printk) - (void *)PRINTK_ADDR;
	sct = (void **)SYS_CALL_TABLE_ADDR + offset / sizeof(void **);

	if(sct[__NR_mount] != (void **)SYS_MOUNT_ADDR + offset / sizeof(void **)) return NULL;
	if(sct[__NR_umount2] != (void **)SYS_UMOUNT_ADDR + offset / sizeof(void **)) return NULL;
#ifdef HAVE_SYS_OLDUMOUNT
	if(sct[__NR_umount] != (void **)SYS_OLDUMOUNT_ADDR + offset / sizeof(void **)) return NULL;
#endif

	LOG_DEBUG("system call table located at 0x%p", sct);

	return sct;
}

#define set_syscall(sys_nr, orig_call_save, new_call) 		\
	orig_call_save = system_call_table[sys_nr];				\
	system_call_table[sys_nr] = new_call;

#define restore_syscall(sys_nr, orig_call_save) system_call_table[sys_nr] = orig_call_save;

static void restore_system_call_table(void){
	unsigned long cr0;

	if(system_call_table){
		LOG_DEBUG("restoring system call table");
		//break back into the syscall table and replace the hooks we stole
		preempt_disable();
		disable_page_protection(&cr0);
		restore_syscall(__NR_mount, orig_mount);
		restore_syscall(__NR_umount2, orig_umount);
#ifdef HAVE_SYS_OLDUMOUNT
		restore_syscall(__NR_umount, orig_oldumount);
#endif
		reenable_page_protection(&cr0);
		preempt_enable();
	}
}

static int hook_system_call_table(void){
	unsigned long cr0;

	//find sys_call_table
	LOG_DEBUG("locating system call table");
	system_call_table = find_sys_call_table();
	if(!system_call_table){
		LOG_WARN("failed to locate system call table, persistence disabled");
		return -ENOENT;
	}

	//break into the syscall table and steal the hooks we need
	preempt_disable();
	disable_page_protection(&cr0);
	set_syscall(__NR_mount, orig_mount, mount_hook);
	set_syscall(__NR_umount2, orig_umount, umount_hook);
#ifdef HAVE_SYS_OLDUMOUNT
	set_syscall(__NR_umount, orig_oldumount, oldumount_hook);
#endif
	reenable_page_protection(&cr0);
	preempt_enable();
	return 0;
}

/***************************BLOCK DEVICE DRIVER***************************/

static int __tracer_add_ref(struct snap_device *dev, int ref_cnt){
	int ret = 0;

	if(!dev){
		ret = -EFAULT;
		LOG_ERROR(ret, "requested snapshot device does not exist");
		goto error;
	}

	atomic_add(ref_cnt, &dev->sd_refs);

error:
	return ret;
}
#define __tracer_open(dev) __tracer_add_ref(dev, 1)
#define __tracer_close(dev) __tracer_add_ref(dev, -1)

#ifdef HAVE_BDOPS_OPEN_INODE
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int snap_open(struct inode *inode, struct file *filp){
	return __tracer_open(inode->i_bdev->bd_disk->private_data);
}

static int snap_release(struct inode *inode, struct file *filp){
	return __tracer_close(inode->i_bdev->bd_disk->private_data);
}
#elif defined HAVE_BDOPS_OPEN_INT
//#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
static int snap_open(struct block_device *bdev, fmode_t mode){
	return __tracer_open(bdev->bd_disk->private_data);
}

static int snap_release(struct gendisk *gd, fmode_t mode){
	return __tracer_close(gd->private_data);
}
#else
static int snap_open(struct block_device *bdev, fmode_t mode){
	return __tracer_open(bdev->bd_disk->private_data);
}

static void snap_release(struct gendisk *gd, fmode_t mode){
	__tracer_close(gd->private_data);
}
#endif

static int dattobd_proc_show(struct seq_file *m, void *v){
	int error, i;
	struct snap_device **dev_ptr = v;
	struct snap_device *dev = NULL;

	//print the header if the "pointer" really an indication to do so
	if(dev_ptr == SEQ_START_TOKEN){
		seq_printf(m, "{\n");
		seq_printf(m, "\t\"version\": \"%s\",\n", DATTOBD_VERSION);
		seq_printf(m, "\t\"devices\": [\n");
	}

	//if the pointer is actually a device print it
	if(dev_ptr != SEQ_START_TOKEN && *dev_ptr != NULL){
		dev = *dev_ptr;

		if(dev->sd_minor != lowest_minor) seq_printf(m, ",\n");
		seq_printf(m, "\t\t{\n");
		seq_printf(m, "\t\t\t\"minor\": %u,\n", dev->sd_minor);
		seq_printf(m, "\t\t\t\"cow_file\": \"%s\",\n", dev->sd_cow_path);
		seq_printf(m, "\t\t\t\"block_device\": \"%s\",\n", dev->sd_bdev_path);
		seq_printf(m, "\t\t\t\"max_cache\": %lu,\n", (dev->sd_cache_size)? dev->sd_cache_size : dattobd_cow_max_memory_default);

		if(!test_bit(UNVERIFIED, &dev->sd_state)){
			seq_printf(m, "\t\t\t\"fallocate\": %llu,\n", ((unsigned long long)dev->sd_falloc_size) * 1024 * 1024);

			if(dev->sd_cow){
				seq_printf(m, "\t\t\t\"seq_id\": %llu,\n", (unsigned long long)dev->sd_cow->seqid);

				seq_printf(m, "\t\t\t\"uuid\": \"");
				for(i = 0; i < COW_UUID_SIZE; i++){
					seq_printf(m, "%02x", dev->sd_cow->uuid[i]);
				}
				seq_printf(m, "\",\n");

				if(dev->sd_cow->version > COW_VERSION_0){
					seq_printf(m, "\t\t\t\"version\": %llu,\n", dev->sd_cow->version);
					seq_printf(m, "\t\t\t\"nr_changed_blocks\": %llu,\n", dev->sd_cow->nr_changed_blocks);
				}
			}
		}

		error = tracer_read_fail_state(dev);
		if(error) seq_printf(m, "\t\t\t\"error\": %d,\n", error);

		seq_printf(m, "\t\t\t\"state\": %lu\n", dev->sd_state);
		seq_printf(m, "\t\t}");
	}

	//print the footer if there are no devices to print or if this device has the highest minor
	if((dev_ptr == SEQ_START_TOKEN && lowest_minor > highest_minor) || (dev && dev->sd_minor == highest_minor)){
		seq_printf(m, "\n\t]\n");
		seq_printf(m, "}\n");
	}

	return 0;
}

static inline void *dattobd_proc_get_idx(loff_t pos){
	if(pos > highest_minor) return NULL;
	return &snap_devices[pos];
}

static void *dattobd_proc_start(struct seq_file *m, loff_t *pos){
	if(*pos == 0) return SEQ_START_TOKEN;
	return dattobd_proc_get_idx(*pos - 1);
}

static void *dattobd_proc_next(struct seq_file *m, void *v, loff_t *pos){
	void *dev = dattobd_proc_get_idx(*pos);
	++*pos;
	return dev;
}

static void dattobd_proc_stop(struct seq_file *m, void *v){
}

static int dattobd_proc_open(struct inode *inode, struct file *filp){
	mutex_lock(&ioctl_mutex);
	return seq_open(filp, &dattobd_seq_proc_ops);
}

static int dattobd_proc_release(struct inode *inode, struct file *file){
	seq_release(inode, file);
	mutex_unlock(&ioctl_mutex);
	return 0;
}

/************************MODULE SETUP AND DESTROY************************/

static void agent_exit(void){
	int i;
	struct snap_device *dev;

	LOG_DEBUG("module exit");

	restore_system_call_table();

	//unregister control device
	LOG_DEBUG("unregistering control device");
	misc_deregister(&snap_control_device);

	//unregister proc info file
	LOG_DEBUG("unregistering /proc file");
	remove_proc_entry(INFO_PROC_FILE, NULL);

	//destroy our snap devices
	LOG_DEBUG("destroying snap devices");
	if(snap_devices){
		tracer_for_each(dev, i){
			if(dev){
				LOG_DEBUG("destroying minor - %d", i);
				tracer_destroy(dev);
			}
		}
		kfree(snap_devices);
		snap_devices = NULL;
	}

	//unregister our block device driver
	LOG_DEBUG("unregistering device driver from the kernel");
	unregister_blkdev(major, DRIVER_NAME);
}
module_exit(agent_exit);

static int __init agent_init(void){
	int ret;

	LOG_DEBUG("module init");

	//init ioctl mutex
	mutex_init(&ioctl_mutex);

	//init minor range
	if(dattobd_max_snap_devices == 0 || dattobd_max_snap_devices > DATTOBD_MAX_SNAP_DEVICES){
		const unsigned int nr_devices = dattobd_max_snap_devices == 0 ? DATTOBD_DEFAULT_SNAP_DEVICES : DATTOBD_MAX_SNAP_DEVICES;
		LOG_WARN("invalid number of snapshot devices (%u), setting to %u", dattobd_max_snap_devices, nr_devices);
		dattobd_max_snap_devices = nr_devices;
	}

	highest_minor = 0;
	lowest_minor = dattobd_max_snap_devices - 1;

	//get a major number for the driver
	LOG_DEBUG("get major number");
	major = register_blkdev(0, DRIVER_NAME);
	if(major <= 0){
		ret = -EBUSY;
		LOG_ERROR(ret, "error requesting major number from the kernel");
		goto error;
	}

	//allocate global device array
	LOG_DEBUG("allocate global device array");
	snap_devices = kzalloc(dattobd_max_snap_devices * sizeof(struct snap_device*), GFP_KERNEL);
	if(!snap_devices){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating global device array");
		goto error;
	}

	//register proc file
	LOG_DEBUG("registering proc file");
	info_proc = proc_create(INFO_PROC_FILE, 0, NULL, &dattobd_proc_fops);
	if(!info_proc){
		ret = -ENOENT;
		LOG_ERROR(ret, "error registering proc file");
		goto error;
	}

	//register control device
	LOG_DEBUG("registering control device");
	ret = misc_register(&snap_control_device);
	if(ret){
		LOG_ERROR(ret, "error registering control device");
		goto error;
	}

	if(dattobd_may_hook_syscalls) (void)hook_system_call_table();

	return 0;

error:
	agent_exit();
	return ret;
}
module_init(agent_init);
