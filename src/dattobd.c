/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "includes.h"
#include "kernel-config.h"
#include "dattobd.h"

//current lowest supported kernel = 2.6.31

//basic information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tom Caputi");
MODULE_DESCRIPTION("Kernel module for supporting block device snapshots and incremental backups.");

#define VERSION_STRING "0.8.13"
MODULE_VERSION(VERSION_STRING);

/*********************************REDEFINED FUNCTIONS*******************************/

#ifndef HAVE_BIO_LIST
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
struct bio_list {
	struct bio *head;
	struct bio *tail;
};

#define BIO_EMPTY_LIST	{ NULL, NULL }
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

#ifndef HAVE_BLKDEV_GET_BY_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
static struct block_device *blkdev_get_by_path(const char *path, fmode_t mode, void *holder){
	struct block_device *bdev;
	int err;

	bdev = lookup_bdev(path);
	if(IS_ERR(bdev))
		return bdev;

	err = blkdev_get(bdev, mode);
	if(err)
		return ERR_PTR(err);

	if((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
		blkdev_put(bdev, mode);
		return ERR_PTR(-EACCES);
	}

	return bdev;
}
#endif

#ifndef HAVE_SUBMIT_BIO_WAIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
struct submit_bio_ret{
	struct completion event;
	int error;
};

static void submit_bio_wait_endio(struct bio *bio, int error){
	struct submit_bio_ret *ret = bio->bi_private;
	ret->error = error;
	complete(&ret->event);
}

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
	#define bio_iter_idx(iter) (iter.bi_idx)
	#define bio_sector(bio) (bio)->bi_iter.bi_sector
	#define bio_size(bio) (bio)->bi_iter.bi_size
	#define bio_idx(bio) (bio)->bi_iter.bi_idx
#endif

#ifndef HAVE_KERN_PATH
static int kern_path(const char *name, unsigned int flags, struct path *path){
	struct nameidata nd;
	int ret = path_lookup(name, flags, &nd);
	if(!ret) *path = nd.path;
	return ret;
}
#endif

#ifdef HAVE_BLKDEV_PUT_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	#define bdev_put(bdev) blkdev_put(bdev);
#else
	#define bdev_put(bdev) blkdev_put(bdev, FMODE_READ);
#endif

#ifndef HAVE_PART_NR_SECTS_READ
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
	#define bdev_size(bdev) (bdev->bd_part->nr_sects)
#else
	#define bdev_size(bdev) part_nr_sects_read(bdev->bd_part)
#endif

#ifndef HAVE_VZALLOC
	#define vzalloc(size) __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL)
#endif

//this is defined in 3.16 and up
#ifndef MIN_NICE
	#define MIN_NICE -20
#endif

//if this isn't defined, we don't need it anyway
#ifndef FMODE_NONOTIFY
	#define FMODE_NONOTIFY 0
#endif

#ifndef HAVE_BLK_QUEUE_MAX_SECTORS
	#define blk_queue_max_sectors(q, sects) blk_queue_max_hw_sectors(q, sects)
#endif

#ifndef HAVE_BLK_SET_STACKING_LIMITS
	#define blk_set_stacking_limits(ql) blk_set_default_limits(ql)
#endif

/*********************************MACRO/PARAMETER DEFINITIONS*******************************/

//printing macros
#ifdef DATTO_DEBUG
	#define LOG_DEBUG(fmt, args...) printk(KERN_DEBUG "datto: " fmt "\n", ## args)
	#define PRINT_BIO(text, bio) LOG_DEBUG(text ": sect = %llu size = %u", (unsigned long long)bio_sector(bio), bio_size(bio))
#else
	#define LOG_DEBUG(fmt, args...)
	#define PRINT_BIO(text, bio)
#endif
#define LOG_WARN(fmt, args...) printk(KERN_WARNING "datto: " fmt "\n", ## args)
#define LOG_ERROR(error, fmt, args...) printk(KERN_ERR "datto: " fmt ": %d\n", ## args, error)

//memory macros
#define get_zeroed_pages(flags, order) __get_free_pages((flags | __GFP_ZERO), order)

//takes a value and the log of the value it should be rounded up to
#define NUM_SEGMENTS(x, log_size) (((x) + (1<<(log_size)) - 1) >> (log_size))
#define ROUND_UP(x, chunk) ((((x) + (chunk) - 1) / (chunk)) * (chunk))
#define ROUND_DOWN(x, chunk) (((x) / (chunk)) * (chunk))

//bitmap macros
#define bitmap_is_marked(bitmap, pos) ((bitmap[(pos) / 8] & (1 << ((pos) % 8))) != 0)
#define bitmap_mark(bitmap, pos) bitmap[(pos) / 8] |= (1 << ((pos) % 8))

//name macros
#define INFO_PROC_FILE "datto-info"
#define DRIVER_NAME "datto"
#define CONTROL_DEVICE_NAME "datto-ctl"
#define SNAP_DEVICE_NAME "datto%d"
#define SNAP_COW_THREAD_NAME_FMT "datto_snap_cow%d"
#define SNAP_MRF_THREAD_NAME_FMT "datto_snap_mrf%d"
#define INC_THREAD_NAME_FMT "datto_inc%d"

//macro for iterating over snap_devices (requires a null check on dev)
#define tracer_for_each(dev, i) for(i = 0, dev = ACCESS_ONCE(snap_devices[i]); i < MAX_SNAP_DEVICES; i++, dev = ACCESS_ONCE(snap_devices[i])) 

//returns true if tracing struct's base device queue matches that of bio 
#define tracer_queue_matches_bio(dev, bio) (bdev_get_queue(dev->sd_base_dev) == bdev_get_queue(bio->bi_bdev))

//returns true if tracing struct's sector range matches the sector of the bio
#define tracer_sector_matches_bio(dev, bio) (bio_sector(bio) >= dev->sd_sect_off && bio_sector(bio) < dev->sd_sect_off + dev->sd_size)

//should be called along with *_queue_matches_bio to be valid. returns true if bio is a write, has a size, 
//tracing struct is in non-fail state, and the device's sector range matches the bio 
#define tracer_should_trace_bio(dev, bio) (bio_data_dir(bio) && bio_size(bio) && !tracer_read_fail_state(dev) && tracer_sector_matches_bio(dev, bio))

//macros for verifying file
#define file_is_on_bdev(file, bdev) ((file)->f_path.mnt->mnt_sb == (bdev)->bd_super)

//macros and flags for the cow manager
#define COW_MAGIC ((uint32_t)4776)

#define COW_CLEAN 0
#define COW_INDEX_ONLY 1
#define COW_VMALLOC_UPPER 2

//macros for defining the offsets of the cow manager header
#define COW_HEADER_SIZE 4096
#define COW_MAGIC_OFFSET 0
#define COW_FLAGS_OFFSET 4
#define COW_FPOS_OFFSET 8
#define COW_FALLOC_OFFSET 16
#define COW_META_SIZE 24

//macros for defining sector and block sizes
#define KERNEL_SECTOR_LOG_SIZE 9
#define KERNEL_SECTOR_SIZE (1 << KERNEL_SECTOR_LOG_SIZE)
#define SECTORS_PER_PAGE (PAGE_SIZE / KERNEL_SECTOR_SIZE)
#define COW_BLOCK_LOG_SIZE 12
#define COW_BLOCK_SIZE (1 << COW_BLOCK_LOG_SIZE)
#define COW_SECTION_SIZE 4096
#define SECTORS_PER_BLOCK (COW_BLOCK_SIZE / KERNEL_SECTOR_SIZE)
#define SECTOR_TO_BLOCK(sect) ((sect) / SECTORS_PER_BLOCK)
#define BLOCK_TO_SECTOR(block) ((block) * SECTORS_PER_BLOCK)

//macros for defining the state of a tracing struct (bit offsets)
#define SNAPSHOT 0
#define ACTIVE 1
#define UNVERIFIED 2

//macros for working with bios
#define bio_flag(bio, flag) (bio->bi_flags |= (1 << flag))
#define BIO_ALREADY_TRACED 20
#define bio_last_sector(bio) (bio_sector(bio) + (bio_size(bio) / KERNEL_SECTOR_SIZE))

//macros for system call hooks
#define CR0_WP 0x00010000

//global module parameters
static bool MAY_HOOK_SYSCALLS = 1;
static unsigned long COW_MAX_MEMORY_DEFAULT = (300*1024*1024);
static unsigned int COW_FALLOCATE_PERCENTAGE_DEFAULT = 10;
static unsigned int MAX_SNAP_DEVICES = 24;

module_param(MAY_HOOK_SYSCALLS, bool, 0);
MODULE_PARM_DESC(MAY_HOOK_SYSCALLS, "if true, allows the kernel module to find and alter the system call table to allow tracing to work across remounts");

module_param(COW_MAX_MEMORY_DEFAULT, ulong, 0);
MODULE_PARM_DESC(COW_MAX_MEMORY_DEFAULT, "default maximum cache size (in bytes)");

module_param(COW_FALLOCATE_PERCENTAGE_DEFAULT, uint, 0);
MODULE_PARM_DESC(COW_FALLOCATE_PERCENTAGE_DEFAULT, "default space allocated to the cow file (as integer percentage)");

module_param(MAX_SNAP_DEVICES, uint, 0);
MODULE_PARM_DESC(MAX_SNAP_DEVICES, "maximum number of tracers available");

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

struct tracing_params{
	struct bio *orig_bio;
	struct snap_device *dev;
	atomic_t refs;
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
	unsigned int sd_refs; //number of users who have this device open
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
};

static long ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int info_proc_open(struct inode*, struct file*);

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

static struct block_device_operations snap_ops = {
	.owner = THIS_MODULE,
	.open = snap_open,
	.release = snap_release,
};

static struct file_operations snap_control_fops = {
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

static struct file_operations info_proc_fops = {
	.owner = THIS_MODULE,
	.open = info_proc_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
};

static int major;
static struct mutex ioctl_mutex;
static struct snap_device **snap_devices;
static struct proc_dir_entry *info_proc;
static void **system_call_table = NULL;

static asmlinkage long (*orig_mount)(char __user *, char __user *, char __user *, unsigned long, void __user *);
static asmlinkage long (*orig_umount)(char __user *, int);
#ifdef HAVE_SYS_OLDUMOUNT
static asmlinkage long (*orig_oldumount)(char __user *);
#endif

/*******************************ATOMIC FUNCTIONS******************************/

static inline int tracer_read_fail_state(struct snap_device *dev){
	smp_mb();
	return atomic_read(&dev->sd_fail_code);
}

static inline void tracer_set_fail_state(struct snap_device *dev, int error){
	smp_mb();
	atomic_cmpxchg(&dev->sd_fail_code, 0, error);
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
		goto copy_string_from_user_error;
	}
	
	*out_ptr = str;
	return 0;
	
copy_string_from_user_error:
	LOG_ERROR(ret, "error copying string from user space");
	*out_ptr = NULL;
	return ret;
}

static int get_setup_params(struct setup_params __user *in, unsigned int *minor, char **bdev_name, char **cow_path, unsigned long *fallocated_space, unsigned long *cache_size){
	int ret;
	struct setup_params params;
	
	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct setup_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying setup_params struct from user space");
		goto get_setup_params_error;
	}
	
	ret = copy_string_from_user((char __user *)params.bdev, bdev_name);
	if(ret)	goto get_setup_params_error;
	
	ret = copy_string_from_user((char __user *)params.cow, cow_path);
	if(ret)	goto get_setup_params_error;
	
	*minor = params.minor;
	*fallocated_space = params.fallocated_space;
	*cache_size = params.cache_size;
	return 0;
	
get_setup_params_error:
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

static int get_reload_params(struct reload_params __user *in, unsigned int *minor, char **bdev_name, char **cow_path, unsigned long *cache_size){
	int ret;
	struct reload_params params;
	
	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct reload_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying reload_params struct from user space");
		goto get_setup_params_error;
	}
	
	ret = copy_string_from_user((char __user *)params.bdev, bdev_name);
	if(ret)	goto get_setup_params_error;
	
	ret = copy_string_from_user((char __user *)params.cow, cow_path);
	if(ret)	goto get_setup_params_error;
	
	*minor = params.minor;
	*cache_size = params.cache_size;
	return 0;
	
get_setup_params_error:
	LOG_ERROR(ret, "error copying reload_params from user space");
	if(*bdev_name) kfree(*bdev_name);
	if(*cow_path) kfree(*cow_path);
	
	*bdev_name = NULL;
	*cow_path = NULL;
	*minor = 0;
	*cache_size = 0;
	return ret;
}

static int get_transition_snap_params(struct transition_snap_params __user *in, unsigned int *minor, char **cow_path, unsigned long *fallocated_space){
	int ret;
	struct transition_snap_params params;
	
	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct transition_snap_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying transition_snap_params struct from user space");
		goto get_transition_snap_params_error;
	}	
	
	ret = copy_string_from_user((char __user *)params.cow, cow_path);
	if(ret)	goto get_transition_snap_params_error;
	
	*minor = params.minor;
	*fallocated_space = params.fallocated_space;
	return 0;
	
get_transition_snap_params_error:
	LOG_ERROR(ret, "error copying transition_snap_params from user space");
	if(*cow_path) kfree(*cow_path);
	
	*cow_path = NULL;
	*minor = 0;
	*fallocated_space = 0;
	return ret;
}

static int get_reconfigure_params(struct reconfigure_params __user *in, unsigned int *minor, unsigned long *cache_size){
	int ret;
	struct reconfigure_params params;
	
	//copy the params struct
	ret = copy_from_user(&params, in, sizeof(struct reconfigure_params));
	if(ret){
		ret = -EFAULT;
		LOG_ERROR(ret, "error copying reconfigure_params struct from user space");
		goto get_reconfigure_params_error;
	}
	
	*minor = params.minor;
	*cache_size = params.cache_size;
	return 0;
	
get_reconfigure_params_error:
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
		
		raw_spin_unlock_wait(&task->pi_lock);
		smp_mb();

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
	filp_close(f, 0);
}

static int file_open(const char *filename, int flags, struct file **filp){
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

static int file_io(struct file *filp, int is_write, void *buf, sector_t offset, unsigned long len){
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
#define file_write(filp, buf, offset, len) file_io(filp, 1, buf, offset, len)
#define file_read(filp, buf, offset, len) file_io(filp, 0, buf, offset, len)

//reimplemented from linux kernel (it isn't exported in the vanilla kernel)
static int do_truncate2(struct dentry *dentry, loff_t length, unsigned int time_attrs, struct file *filp){ 
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

static int file_truncate(struct file *filp, loff_t len){
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
	
	ret = do_truncate2(dentry, len, ATTR_MTIME|ATTR_CTIME, filp);
	
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

static int file_allocate(struct file *f, uint64_t offset, uint64_t length){
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

static int __file_unlink(struct file *filp, int close){
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
	
	if(close && !ret) file_close(filp);
	
file_unlink_mnt_error:
	iput(dir_inode);
	dput(file_dentry);
	
	return ret;
}
#define file_unlink(filp) __file_unlink(filp, 0)
#define file_unlink_and_close(filp) __file_unlink(filp, 1)

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

static int path_get_absolute_pathname(struct path *path, char **buf, int *len_res){
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

static int pathname_to_absolute(char *pathname, char **buf, int *len_res){
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

static int pathname_concat(char *pathname1, char *pathname2, char **path_out){
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

static int user_mount_pathname_concat(char __user *user_mount_path, char *rel_path, char **path_out){
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
	if(ret) goto cow_load_section_error;
	
	ret = file_read(cm->filp, cm->sects[sect_idx].mappings, cm->sect_size*sect_idx*8 + COW_HEADER_SIZE, cm->sect_size*8);
	if(ret) goto cow_load_section_error;
	
	return 0;

cow_load_section_error:
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
	char header_buf[COW_META_SIZE];
	uint32_t magic = COW_MAGIC;
	
	if(is_clean) cm->flags |= (1 << COW_CLEAN);
	else cm->flags &= ~(1 << COW_CLEAN);
	
	memcpy(&header_buf[COW_MAGIC_OFFSET], &magic, sizeof(uint32_t));
	memcpy(&header_buf[COW_FLAGS_OFFSET], &cm->flags, sizeof(uint32_t));
	memcpy(&header_buf[COW_FPOS_OFFSET], &cm->curr_pos, sizeof(uint64_t));
	memcpy(&header_buf[COW_FALLOC_OFFSET], &cm->file_max, sizeof(uint64_t));
	
	ret = file_write(cm->filp, header_buf, 0, COW_META_SIZE);
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
	char header_buf[COW_META_SIZE];
	uint32_t magic, flags;
	uint64_t falloc_size, fpos;
	
	ret = file_read(cm->filp, header_buf, 0, COW_META_SIZE);
	if(ret) goto cow_open_header_error;
	
	memcpy(&magic, &header_buf[COW_MAGIC_OFFSET], sizeof(uint32_t));
	memcpy(&flags, &header_buf[COW_FLAGS_OFFSET], sizeof(uint32_t));
	memcpy(&fpos, &header_buf[COW_FPOS_OFFSET], sizeof(uint64_t));
	memcpy(&falloc_size, &header_buf[COW_FALLOC_OFFSET], sizeof(uint64_t));
		
	if(magic != COW_MAGIC){
		ret = -EINVAL;
		LOG_ERROR(-EINVAL, "bad magic number found in cow file: %lu", ((unsigned long)magic));
		goto cow_open_header_error;
	}
	
	if(!(flags & (1 << COW_CLEAN))){
		ret = -EINVAL;
		LOG_ERROR(-EINVAL, "cow file not left in clean state: %lu", ((unsigned long)flags));
		goto cow_open_header_error;
	}
	
	if(((flags & (1 << COW_INDEX_ONLY)) && !index_only) || (!(flags & (1 << COW_INDEX_ONLY)) && index_only)){
		ret = -EINVAL;
		LOG_ERROR(-EINVAL, "cow file not left in %s state: %lu", ((index_only)? "index only" : "data tracking"), (unsigned long)flags);
		goto cow_open_header_error;
	}
	
	LOG_DEBUG("cow header opened with file pos = %llu", ((unsigned long long)fpos));
	
	if(reset_vmalloc) cm->flags = flags & ~(1 << COW_VMALLOC_UPPER);
	else cm->flags = flags;
	
	cm->curr_pos = fpos;
	cm->file_max = falloc_size;
	
	ret = __cow_write_header_dirty(cm);
	if(ret) goto cow_open_header_error;
	
	return 0;
	
cow_open_header_error:
	LOG_ERROR(ret, "error opening cow manager header");
	return ret;
}

static void cow_free(struct cow_manager *cm){
	unsigned long i;
	
	for(i=0; i<cm->total_sects; i++){
		if(cm->sects[i].mappings) free_pages((unsigned long)cm->sects[i].mappings, cm->log_sect_pages);
	}
	
	if(cm->filp) file_unlink_and_close(cm->filp);
	
	if(cm->sects){
		if(cm->flags & (1 << COW_VMALLOC_UPPER)) vfree(cm->sects);
		else kfree(cm->sects);
	}
	
	kfree(cm);
}

static int cow_sync_and_free(struct cow_manager *cm){
	int ret;
	
	ret = __cow_sync_and_free_sections(cm, 0);
	if(ret)	goto cow_sync_and_free_error;
	
	ret = __cow_close_header(cm);
	if(ret) goto cow_sync_and_free_error;
	
	if(cm->filp) file_close(cm->filp);
	
	if(cm->sects){
		if(cm->flags & (1 << COW_VMALLOC_UPPER)) vfree(cm->sects);
		else kfree(cm->sects);
	}
	
	kfree(cm);
	
	return 0;
	
cow_sync_and_free_error:
	LOG_ERROR(ret, "error while syncing and freeing cow manager");
	cow_free(cm);
	return ret;
}

static int cow_sync_and_close(struct cow_manager *cm){
	int ret;
	
	ret = __cow_sync_and_free_sections(cm, 0);
	if(ret)	goto cow_sync_and_close_error;
	
	ret = __cow_close_header(cm);
	if(ret) goto cow_sync_and_close_error;
	
	if(cm->filp) file_close(cm->filp);
	cm->filp = NULL;
	
	return 0;
	
cow_sync_and_close_error:
	LOG_ERROR(ret, "error while syncing and closing cow manager");
	cow_free(cm);
	return ret;
}

static int cow_reopen(struct cow_manager *cm, char *pathname){
	int ret;
	
	LOG_DEBUG("reopening cow file");
	ret = file_open(pathname, 0, &cm->filp);
	if(ret) goto cow_reopen_error;
	
	LOG_DEBUG("opening cow header");
	ret = __cow_open_header(cm, (cm->flags & (1 << COW_INDEX_ONLY)), 0);
	if(ret)	goto cow_reopen_error;
	
	return 0;

cow_reopen_error:
	LOG_ERROR(ret, "error reopening cow manager");
	if(cm->filp) file_close(cm->filp);
	cm->filp = NULL;
	
	return ret;
}

static unsigned long __cow_calculate_allowed_sects(unsigned long cache_size, unsigned long total_sects){
	if(cache_size <= (total_sects * sizeof(struct cow_section))) return 0;
	else return (cache_size - (total_sects * sizeof(struct cow_section))) / (COW_SECTION_SIZE * 8);
}

static int cow_reload(char *path, uint64_t elements, unsigned long sect_size, unsigned long cache_size, int index_only, struct cow_manager **cm_out){
	int ret;
	unsigned long i;
	struct cow_manager *cm;
	
	LOG_DEBUG("allocating cow manager");
	cm = kzalloc(sizeof(struct cow_manager), GFP_KERNEL);
	if(!cm){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating cow manager");
		goto cow_reload_error;
	}
	
	LOG_DEBUG("opening cow file");
	ret = file_open(path, 0, &cm->filp);
	if(ret) goto cow_reload_error;
	
	cm->allocated_sects = 0;
	cm->sect_size = sect_size;
	cm->log_sect_pages = get_order(sect_size*8);
	cm->total_sects = NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
	cm->allowed_sects = __cow_calculate_allowed_sects(cache_size, cm->total_sects);
	cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size*8));
	
	ret = __cow_open_header(cm, index_only, 1);
	if(ret)	goto cow_reload_error;
	
	LOG_DEBUG("allocating cow manager array (%lu sections)", cm->total_sects);
	cm->sects = kzalloc((cm->total_sects) * sizeof(struct cow_section), GFP_KERNEL);
	if(!cm->sects){
		//try falling back to vmalloc
		cm->flags |= (1 << COW_VMALLOC_UPPER);
		cm->sects = vzalloc((cm->total_sects) * sizeof(struct cow_section));
		if(!cm->sects){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating cow manager sects array");
			goto cow_reload_error;
		}
	}
	
	for(i=0; i<cm->total_sects; i++){
		cm->sects[i].has_data = 1;
	}

	*cm_out = cm;
	return 0;
	
cow_reload_error:
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

static int cow_init(char *path, uint64_t elements, unsigned long sect_size, unsigned long cache_size, uint64_t file_max, struct cow_manager **cm_out){
	int ret;
	struct cow_manager *cm;
	
	LOG_DEBUG("allocating cow manager");
	cm = kzalloc(sizeof(struct cow_manager), GFP_KERNEL);
	if(!cm){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating cow manager");
		goto cow_init_error;
	}
	
	LOG_DEBUG("creating cow file");
	ret = file_open(path, O_CREAT | O_TRUNC, &cm->filp);
	if(ret)	goto cow_init_error;
	
	cm->flags = 0;
	cm->allocated_sects = 0;
	cm->file_max = file_max;
	cm->sect_size = sect_size;
	cm->log_sect_pages = get_order(sect_size*8);
	cm->total_sects = NUM_SEGMENTS(elements, cm->log_sect_pages + PAGE_SHIFT - 3);
	cm->allowed_sects = __cow_calculate_allowed_sects(cache_size, cm->total_sects);
	cm->data_offset = COW_HEADER_SIZE + (cm->total_sects * (sect_size*8));
	cm->curr_pos = cm->data_offset / COW_BLOCK_SIZE;
	
	ret = __cow_write_header_dirty(cm);
	if(ret)	goto cow_init_error;
	
	LOG_DEBUG("allocating cow manager array (%lu sections)", cm->total_sects);
	cm->sects = kzalloc((cm->total_sects) * sizeof(struct cow_section), GFP_KERNEL);
	if(!cm->sects){
		//try falling back to vmalloc
		cm->flags |= (1 << COW_VMALLOC_UPPER);
		cm->sects = vzalloc((cm->total_sects) * sizeof(struct cow_section));
		if(!cm->sects){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating cow manager sects array");
			goto cow_init_error;
		}
	}
	
	LOG_DEBUG("allocating cow file (%llu bytes)", (unsigned long long)file_max);
	ret = file_allocate(cm->filp, 0, file_max);
	if(ret) goto cow_init_error;

	*cm_out = cm;
	return 0;
	
cow_init_error:
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
			if(ret)	goto cow_read_mapping_error;
		}
	}
	
	*out = cm->sects[sect_idx].mappings[sect_pos];
	
	if(cm->allocated_sects > cm->allowed_sects){
		ret = __cow_cleanup_mappings(cm);
		if(ret)	goto cow_read_mapping_error;
	}
	
	return 0;
	
cow_read_mapping_error:
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
			if(ret)	goto cow_write_mapping_error;
		}else{
			ret = __cow_load_section(cm, sect_idx);
			if(ret) goto cow_write_mapping_error;
		}
	}

	cm->sects[sect_idx].mappings[sect_pos] = val;
	
	if(cm->allocated_sects > cm->allowed_sects){
		ret = __cow_cleanup_mappings(cm);
		if(ret) goto cow_write_mapping_error;
	}

	return 0;
	
cow_write_mapping_error:
	LOG_ERROR(ret, "error writing cow mapping");
	return ret;
}
#define __cow_write_current_mapping(cm, pos) __cow_write_mapping(cm, pos, (cm)->curr_pos)
#define cow_write_filler_mapping(cm, pos) __cow_write_mapping(cm, pos, 1)

static int __cow_write_data(struct cow_manager *cm, void *buf){
	int ret;
	
	if(cm->curr_pos * COW_BLOCK_SIZE >= cm->file_max){
		ret = -EFBIG;
		LOG_ERROR(ret, "cow file max size exceeded");
		goto cow_write_data_error;
	}
	
	ret = file_write(cm->filp, buf, cm->curr_pos * COW_BLOCK_SIZE, COW_BLOCK_SIZE);
	if(ret) goto cow_write_data_error;
	
	cm->curr_pos++;
	
	return 0;
	
cow_write_data_error:
	LOG_ERROR(ret, "error writing cow data");
	return ret;
}

static int cow_write_current(struct cow_manager *cm, uint64_t block, void *buf){
	int ret;
	uint64_t block_mapping;
	
	//read this mapping from the cow manager
	ret = cow_read_mapping(cm, block, &block_mapping);
	if(ret)	goto cow_write_current_error;
	
	//if the block mapping already exists return so we don't overwrite it
	if(block_mapping) return 0;
	
	//write the mapping
	ret = __cow_write_current_mapping(cm, block);
	if(ret)	goto cow_write_current_error;
	
	//write the data
	ret = __cow_write_data(cm, buf);
	if(ret) goto cow_write_current_error;
	
	return 0;
	
cow_write_current_error:
	LOG_ERROR(ret, "error writing cow data and mapping");
	return ret;
}

static int cow_read_data(struct cow_manager *cm, void *buf, uint64_t block_pos, unsigned int block_off, unsigned int len){
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

static inline int sset_list_empty(struct sset_list *sl){
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

static int bio_queue_empty(struct bio_queue *bq){
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

/****************************SSET QUEUE FUNCTIONS****************************/

static void sset_queue_init(struct sset_queue *sq){
	sset_list_init(&sq->ssets);
	spin_lock_init(&sq->lock);
	init_waitqueue_head(&sq->event);
}

static int sset_queue_empty(struct sset_queue *sq){
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
	
	tp = kmalloc(1 * sizeof(struct tracing_params), GFP_NOIO);
	if(!tp){
		LOG_ERROR(-ENOMEM, "error allocating tracing parameters struct");
		*tp_out = tp;
		return -ENOMEM;
	}
	
	tp->dev = dev;
	tp->orig_bio = bio;;
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
		PRINT_BIO("queueing bio", tp->orig_bio);
		
		bio_queue_add(&tp->dev->sd_orig_bios, tp->orig_bio);
		kfree(tp);
	}
}

/****************************BIO HELPER FUNCTIONS*****************************/

static inline struct inode *page_get_inode(struct page *pg){
	if(!pg->mapping) return NULL;
	if((unsigned long)pg->mapping & PAGE_MAPPING_ANON) return NULL;
	if(!pg->mapping->host) return NULL;
	return pg->mapping->host;
}

static int bio_needs_cow(struct bio *bio, struct inode *inode){
	bio_iter_t iter;
	bio_iter_bvec_t bvec;
	
	//check the inode of each page return true if it does not match our cow file
	bio_for_each_segment(bvec, bio, iter){
		if(page_get_inode(bio_iter_page(bio, iter)) != inode) return 1;
	}
	
	return 0;
}

static void bio_free_clone(struct bio *bio){
	int i;
	
	for(i = 0; i < bio->bi_vcnt; i++){
		if(bio->bi_io_vec[i].bv_page) __free_page(bio->bi_io_vec[i].bv_page);
	}
	bio_put(bio);
}

static int bio_make_read_clone(struct block_device *bdev, sector_t sect, unsigned int pages, struct bio **bio_out, unsigned int *bytes_added){
	int ret;
	struct bio *new_bio;
	struct page *pg;
	unsigned int i, bytes, total = 0;
	
	//allocate bio clone
	new_bio = bio_alloc(GFP_NOIO, pages);
	if(!new_bio){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating bio clone");
		goto bio_make_read_clone_error;
	}
	
	//populate read bio
	new_bio->bi_bdev = bdev;
	new_bio->bi_rw = READ;
	bio_sector(new_bio) = sect;
	bio_idx(new_bio) = 0;
	
	//fill the bio with pages
	for(i = 0; i < pages; i++){
		//allocate a page and add it to our bio
		pg = alloc_page(GFP_NOIO);
		if(!pg){
			ret = -ENOMEM;
			LOG_ERROR(ret, "error allocating read bio page %u", i);
			goto bio_make_read_clone_error;
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
	
bio_make_read_clone_error:
	if(ret) LOG_ERROR(ret, "error creating read clone of write bio");
	if(new_bio) bio_free_clone(new_bio);
	
	*bytes_added = 0;
	*bio_out = NULL;
	return ret;
}

/*******************BIO / SECTOR_SET PROCESSING LOGIC***********************/

static int snap_handle_read_bio(struct snap_device *dev, struct bio *bio){
	int ret;
	bio_iter_t iter;
	bio_iter_bvec_t bvec;
	void *orig_private;
	bio_end_io_t *orig_end_io;
	char *data;
	unsigned int bio_orig_idx, bio_orig_size, bytes_written;
	uint64_t block_mapping, curr_byte, curr_end_byte = bio_sector(bio) * KERNEL_SECTOR_SIZE;
	
	//save the original state of the bio
	orig_private = bio->bi_private;
	orig_end_io = bio->bi_end_io;
	bio_orig_idx = bio_idx(bio);
	bio_orig_size = bio_size(bio);
	
	bio->bi_bdev = dev->sd_base_dev;
	
	//submit the bio to the base device and wait for completion
	ret = submit_bio_wait(READ_SYNC, bio);
	if(ret){
		LOG_ERROR(ret, "error reading from base device for read");
		goto snap_handle_bio_read_out;
	}
	
	//iterate over all the segments and fill the bio. this more complex than writing since we don't have the block aligned guarantee
	bio_for_each_segment(bvec, bio, iter){
		//reset the number of bytes we have written to this bio_vec
		bytes_written = 0;
		
		//map the page into kernel space
		data = kmap(bio_iter_page(bio, iter));
		
		//while we still have data left to be written into the page
		while(bytes_written < bio_iter_len(bio, iter)){
			//find the start and stop byte for our next write
			curr_byte = curr_end_byte;
			curr_end_byte += min(COW_BLOCK_SIZE - (curr_byte % COW_BLOCK_SIZE), ((uint64_t)bio_iter_len(bio, iter)));
			
			//check if the mapping exists
			ret = cow_read_mapping(dev->sd_cow, curr_byte / COW_BLOCK_SIZE, &block_mapping);
			if(ret){
				kunmap(bio_iter_page(bio, iter));
				goto snap_handle_bio_read_out;
			}
			
			//if the mapping exists, read it into the page, overwriting the live data
			if(block_mapping){
				ret = cow_read_data(dev->sd_cow, data + bio_iter_offset(bio, iter) + bytes_written, block_mapping, curr_byte % COW_BLOCK_SIZE, curr_end_byte - curr_byte);
				if(ret){
					kunmap(bio_iter_page(bio, iter));
					goto snap_handle_bio_read_out;
				}
			}
			//increment the number of bytes we have written
			bytes_written += curr_end_byte - curr_byte;
		}
		
		//unmap the page from kernel space
		kunmap(bio_iter_page(bio, iter));
	}
	
snap_handle_bio_read_out:
	if(ret) LOG_ERROR(ret, "error handling read bio");
	
	//revert bio's original data
	bio->bi_private = orig_private;
	bio->bi_end_io = orig_end_io;
	bio_idx(bio) = bio_orig_idx;
	bio_size(bio) = bio_orig_size;
	
#ifdef HAVE_BIO_BI_REMAINING
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
	atomic_inc(&bio->bi_remaining);
#endif
	return ret;
}

static int snap_handle_write_bio(struct snap_device *dev, struct bio *bio){
	int ret;
	bio_iter_t iter;
	bio_iter_bvec_t bvec;
	char *data;
	sector_t start_block, end_block = SECTOR_TO_BLOCK(bio_sector(bio));
	
	PRINT_BIO("writing bio", bio);
	
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
				goto snap_handle_bio_write_error;
			}
		}
		
		//unmap the page
		kunmap(bio_iter_page(bio, iter));
	}
	
	PRINT_BIO("wrote bio", bio);
	
	return 0;
	
snap_handle_bio_write_error:
	LOG_ERROR(ret, "error handling write bio");
	return ret;
}

static int inc_handle_sset(struct snap_device *dev, struct sector_set *sset){
	int ret;
	sector_t start_block = SECTOR_TO_BLOCK(sset->sect);
	sector_t end_block = NUM_SEGMENTS(sset->sect + sset->len, COW_BLOCK_LOG_SIZE - KERNEL_SECTOR_LOG_SIZE);
	
	for(; start_block < end_block; start_block++){
		ret = cow_write_filler_mapping(dev->sd_cow, start_block);
		if(ret) goto inc_handle_sset_error;
	}
	
	return 0;
	
inc_handle_sset_error:
	LOG_ERROR(ret, "error handling sset");
	return ret;
}

static int snap_mrf_thread(void *data){
#ifdef HAVE_MAKE_REQUEST_FN_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
	int ret;
#endif
	struct snap_device *dev = data;
	struct bio_queue *bq = &dev->sd_orig_bios;
	struct bio *bio;

	//give this thread the highest priority we are allowed
	set_user_nice(current, MIN_NICE);
		
	while(!kthread_should_stop() || !bio_queue_empty(bq)) {
		//wait for a bio to process or a kthread_stop call
		wait_event_interruptible(bq->event, kthread_should_stop() || !bio_queue_empty(bq));
		if(bio_queue_empty(bq)) continue;
		
		//safely dequeue a bio
		bio = bio_queue_dequeue(bq);
		
		//submit the original bio to the block IO layer
		bio_flag(bio, BIO_ALREADY_TRACED);
	
#ifdef HAVE_MAKE_REQUEST_FN_INT	
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
		ret = dev->sd_orig_mrf(bdev_get_queue(bio->bi_bdev), bio);
		if(ret) generic_make_request(bio);
#else
		dev->sd_orig_mrf(bdev_get_queue(bio->bi_bdev), bio);
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
	
	while(!kthread_should_stop() || !bio_queue_empty(bq)) {
		//wait for a bio to process or a kthread_stop call
		wait_event_interruptible(bq->event, kthread_should_stop() || !bio_queue_empty(bq));

		if(!is_failed && tracer_read_fail_state(dev)){
			LOG_DEBUG("error detected in cow thread, cleaning up cow");
			is_failed = 1;
			if(dev->sd_cow){
				cow_free(dev->sd_cow);
				dev->sd_cow = NULL;
			}
		}

		if(bio_queue_empty(bq)) continue;
		
		//safely dequeue a bio
		bio = bio_queue_dequeue(bq);
		
		//pass bio to handler
		if(!bio_data_dir(bio)){
			//if we're in the fail state just send back an IO error and free the bio
			if(is_failed){
				bio_endio(bio, -EIO); //end the bio with an IO error
				continue;
			}
			
			ret = snap_handle_read_bio(dev, bio);		
			if(ret){
				LOG_ERROR(ret, "error handling read bio in kernel thread");
				tracer_set_fail_state(dev, ret);
			}
			
			bio_endio(bio, (ret)? -EIO : 0);
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
			if(dev->sd_cow){
				cow_free(dev->sd_cow);
				dev->sd_cow = NULL;
			}
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

static void on_bio_read_complete(struct bio *bio, int err){
	int ret;
	unsigned short i;
	struct tracing_params *tp = bio->bi_private;
	struct snap_device *dev = tp->dev;
	
	//check for read errors
	if(err){
		ret = err;
		LOG_ERROR(ret, "error reading from base device for copy on write");
		goto on_bio_read_complete_error;
	}
	
	//check to make sure the bio is up to date
	if(!test_bit(BIO_UPTODATE, &bio->bi_flags)){
		ret = -EIO;
		LOG_ERROR(ret, "error reading from base device for copy on write (not up to date)");
		goto on_bio_read_complete_error;
	}
	
	//change the bio into a write bio
	bio->bi_rw |= WRITE;
	
	//reset the bio iterator to its original state
	bio_sector(bio) = bio_sector(bio) - (bio->bi_vcnt * SECTORS_PER_PAGE) - dev->sd_sect_off;
	bio_size(bio) = bio->bi_vcnt * PAGE_SIZE;
	bio_idx(bio) = 0;
	
	for(i = 0; i < bio->bi_vcnt; i++){
		bio->bi_io_vec[i].bv_len = PAGE_SIZE;
		bio->bi_io_vec[i].bv_offset = 0;
	}
		
	//queue cow bio for processing by kernel thread
	bio_queue_add(&dev->sd_cow_bios, bio);
	smp_wmb();

	//put a reference to the tp (will queue the orig_bio if nobody else is using it)
	tp_put(tp);

	return;

on_bio_read_complete_error:
	LOG_ERROR(ret, "error during bio read complete callback");
	tracer_set_fail_state(dev, ret);
	tp_put(tp);
	bio_free_clone(bio);
}

static int snap_trace_bio(struct snap_device *dev, struct bio *bio){
	int ret;
	struct bio *new_bio = NULL;
	struct tracing_params *tp = NULL;
	sector_t start_sect, end_sect;
	unsigned int bytes, pages;
	
	//if we don't need to cow this bio just call the real mrf normally
	if(!bio_needs_cow(bio, dev->sd_cow_inode)){
#ifdef HAVE_MAKE_REQUEST_FN_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
		return dev->sd_orig_mrf(bdev_get_queue(bio->bi_bdev), bio);
#else
		dev->sd_orig_mrf(bdev_get_queue(bio->bi_bdev), bio);
		return 0;
#endif
	}
	
	//the cow manager works in 4096 byte blocks, so read clones must also be 4096 byte aligned
	start_sect = ROUND_DOWN(bio_sector(bio) - dev->sd_sect_off, SECTORS_PER_BLOCK) + dev->sd_sect_off;
	end_sect = ROUND_UP(bio_sector(bio) + (bio_size(bio) / KERNEL_SECTOR_SIZE) - dev->sd_sect_off, SECTORS_PER_BLOCK) + dev->sd_sect_off;
	pages = (end_sect - start_sect) / SECTORS_PER_PAGE;
	
	//allocate tracing_params struct to hold all pointers we will need across contexts
	ret = tp_alloc(dev, bio, &tp);
	if(ret) goto snap_trace_bio_error;
	
retry:
	//allocate and populate read bio clone. This bio may not have all the pages we need due to queue restrictions
	ret = bio_make_read_clone(bio->bi_bdev, start_sect, pages, &new_bio, &bytes);
	if(ret) goto snap_trace_bio_error;
	
	//set pointers for read clone
	tp_get(tp);
	new_bio->bi_private = tp;
	new_bio->bi_end_io = on_bio_read_complete;
		
	//submit the bios
	PRINT_BIO("submitting bio", new_bio);
	submit_bio(0, new_bio);
	
	//if our bio didn't cover the entire clone we must keep creating bios until we have
	if(bytes / PAGE_SIZE < pages){
		start_sect += bytes / KERNEL_SECTOR_SIZE;
		pages -= bytes / PAGE_SIZE;
		goto retry;
	}
	
	//drop our reference to the tp
	tp_put(tp);
	
	return 0;

snap_trace_bio_error:
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
	
	bio_for_each_segment(bvec, bio, iter){
		if(page_get_inode(bio_iter_page(bio, iter)) != dev->sd_cow_inode){
			if(!is_initialized){
				is_initialized = 1;
				start_sect = end_sect;
			}
		}else{
			if(is_initialized && end_sect - start_sect > 0){
				ret = inc_make_sset(dev, start_sect, end_sect - start_sect);
				if(ret)	goto inc_trace_bio_out;
			}
			is_initialized = 0;
		}
		end_sect += (bio_iter_len(bio, iter) >> 9);
	}
	
	if(is_initialized && end_sect - start_sect > 0){
		ret = inc_make_sset(dev, start_sect, end_sect - start_sect);
		if(ret)	goto inc_trace_bio_out;
	}

inc_trace_bio_out:
	if(ret){
		LOG_ERROR(ret, "error tracing bio for incremental");
		tracer_set_fail_state(dev, ret);
		ret = 0;
	}

	//call the original mrf
#ifdef HAVE_MAKE_REQUEST_FN_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
	ret = dev->sd_orig_mrf(bdev_get_queue(bio->bi_bdev), bio);
#else
	dev->sd_orig_mrf(bdev_get_queue(bio->bi_bdev), bio);
#endif
	
	return ret;
}

#ifdef HAVE_MAKE_REQUEST_FN_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
static int tracing_mrf(struct request_queue *q, struct bio *bio){
	int i, ret = 0;
	struct snap_device *dev;
	make_request_fn *orig_mrf = NULL;
	
	smp_rmb();
	tracer_for_each(dev, i){
		if(!dev || test_bit(UNVERIFIED, &dev->sd_state) || !tracer_queue_matches_bio(dev, bio)) continue;
		
		orig_mrf = dev->sd_orig_mrf;
		if(bio_flagged(bio, BIO_ALREADY_TRACED)){
			bio->bi_flags &= ~BIO_ALREADY_TRACED;
			goto tracing_mrf_call_orig;
		}
		
		if(tracer_should_trace_bio(dev, bio)){
			if(test_bit(SNAPSHOT, &dev->sd_state)) ret = snap_trace_bio(dev, bio);
			else ret = inc_trace_bio(dev, bio);
			goto tracing_mrf_out;
		}
	}
	
tracing_mrf_call_orig:
	if(orig_mrf) ret = orig_mrf(q, bio);
	else LOG_ERROR(-EFAULT, "error finding original_mrf");
	
tracing_mrf_out:
	return ret;
}

static int snap_mrf(struct request_queue *q, struct bio *bio){
	struct snap_device *dev = q->queuedata;

	//if a write request somehow gets sent in, discard it
	if(bio_data_dir(bio)){
		bio_endio(bio, -EOPNOTSUPP);
		return 0;
	}else if(tracer_read_fail_state(dev)){
		bio_endio(bio, -EIO);
		return 0;
	}else if(!test_bit(ACTIVE, &dev->sd_state)){
		bio_endio(bio, -EBUSY);
		return 0;
	}
	
	//queue bio for processing by kernel thread
	bio_queue_add(&dev->sd_cow_bios, bio);
	
	return 0;
}
#else
static void tracing_mrf(struct request_queue *q, struct bio *bio){
	int i;
	struct snap_device *dev;
	make_request_fn *orig_mrf = NULL;
	
	smp_rmb();
	tracer_for_each(dev, i){
		if(!dev || test_bit(UNVERIFIED, &dev->sd_state) || !tracer_queue_matches_bio(dev, bio)) continue;
		
		orig_mrf = dev->sd_orig_mrf;
		if(bio_flagged(bio, BIO_ALREADY_TRACED)){
			bio->bi_flags &= ~BIO_ALREADY_TRACED;
			goto tracing_mrf_call_orig;
		}
		
		if(tracer_should_trace_bio(dev, bio)){
			if(test_bit(SNAPSHOT, &dev->sd_state)) snap_trace_bio(dev, bio);
			else inc_trace_bio(dev, bio);
			goto tracing_mrf_out;
		}
	}
	
tracing_mrf_call_orig:
	if(orig_mrf) orig_mrf(q, bio);
	else LOG_ERROR(-EFAULT, "error finding original_mrf");
	
tracing_mrf_out:
	return;
}

static void snap_mrf(struct request_queue *q, struct bio *bio){
	struct snap_device *dev = q->queuedata;
	
	//if a write request somehow gets sent in, discard it
	if(bio_data_dir(bio)){
		bio_endio(bio, -EOPNOTSUPP);
		return;
	}else if(tracer_read_fail_state(dev)){
		bio_endio(bio, -EIO);
		return;
	}else if(!test_bit(ACTIVE, &dev->sd_state)){
		bio_endio(bio, -EBUSY);
		return;
	}

	//queue bio for processing by kernel thread
	bio_queue_add(&dev->sd_cow_bios, bio);
}
#endif

/*******************************SETUP HELPER FUNCTIONS********************************/

static int string_copy(char *str, char **dest){
	int ret, len;
	char *out;
	
	len = strlen(str);
	out = kmalloc(len + 1, GFP_KERNEL);
	if(!out){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating destination for string copy");
		goto string_copy_error;
	}
	
	strncpy(out, str, len);
	out[len] = '\0';
	
	*dest = out;
	return 0;
	
string_copy_error:
	LOG_ERROR(ret, "error copying string");
	if(out)	kfree(out);
	
	*dest = NULL;
	return ret;
}

static int bdev_is_already_traced(struct block_device *bdev){
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

static int __tracer_should_reset_mrf(struct snap_device *dev){
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
#ifdef HAVE_THAW_BDEV_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
	int ret;
#endif
	struct super_block *sb = bdev->bd_super;
	
	if(sb){
		//freeze and sync block device
		LOG_DEBUG("freezing block device");
		sb = freeze_bdev(bdev);
		if(!sb){
			LOG_ERROR(-EFAULT, "error freezing block device: null");
			return -EFAULT;
		}else if(IS_ERR(sb)){
			LOG_ERROR((int)PTR_ERR(sb), "error freezing block device: error");
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
	
	if(sb){
		//thaw the block device
		LOG_DEBUG("thawing block device");
#ifndef HAVE_THAW_BDEV_INT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
		thaw_bdev(bdev, sb);
#else
		ret = thaw_bdev(bdev, sb);
		if(ret){
			LOG_ERROR(ret, "error thawing block device");
			//we can't reasonably undo what we've done at this point, and we've replaced the mrf.
			//pretend we succeeded so we don't break the block device
		}
#endif
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
		goto tracer_alloc_error;
	}
	
	__tracer_init(dev);
	
	*dev_ptr = dev;
	return 0;

tracer_alloc_error:
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
		bdev_put(dev->sd_base_dev);
		dev->sd_base_dev = NULL;
	}
}

static int __tracer_setup_base_dev(struct snap_device *dev, char *bdev_path){
	int ret;
	
	//open the base block device
	LOG_DEBUG("finding block device");
	dev->sd_base_dev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);
	if(IS_ERR(dev->sd_base_dev)){
		ret = PTR_ERR(dev->sd_base_dev);
		dev->sd_base_dev = NULL;
		LOG_ERROR(ret, "error finding block device '%s'", bdev_path);
		goto tracer_setup_base_dev_error;
	}else if(!dev->sd_base_dev->bd_disk){
		ret = -EFAULT;
		LOG_ERROR(ret, "error finding block device gendisk");
		goto tracer_setup_base_dev_error;
	}
	
	//check block device is not already being traced
	LOG_DEBUG("checking block device is not already being traced");
	if(bdev_is_already_traced(dev->sd_base_dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "block device is already being traced");
		goto tracer_setup_base_dev_error;
	}
	
	//fetch the absolute pathname for the base device
	LOG_DEBUG("fetching the absolute pathname for the base device");
	ret = pathname_to_absolute(bdev_path, &dev->sd_bdev_path, NULL);
	if(ret)	goto tracer_setup_base_dev_error;
	
	//check if device represents a partition, calculate size and offset
	LOG_DEBUG("calculating block device size and offset");
	if(dev->sd_base_dev->bd_contains != dev->sd_base_dev){
		dev->sd_sect_off = dev->sd_base_dev->bd_part->start_sect;
		dev->sd_size = bdev_size(dev->sd_base_dev);
	}else{
		dev->sd_sect_off = 0;
		dev->sd_size = get_capacity(dev->sd_base_dev->bd_disk);
	}
	
	return 0;
	
tracer_setup_base_dev_error:
	LOG_ERROR(ret, "error setting up base block device");
	__tracer_destroy_base_dev(dev);
	return ret;
}

static void __tracer_copy_base_dev(struct snap_device *src, struct snap_device *dest){
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

static int __tracer_setup_cow(struct snap_device *dev, struct block_device *bdev, char *cow_path, sector_t size, unsigned long fallocated_space, unsigned long cache_size, int open_method){
	int ret;
	uint64_t max_file_size;
	
	if(open_method == 3){
		//reopen the cow manager
		LOG_DEBUG("reopening the cow manager with file '%s'", cow_path);
		ret = cow_reopen(dev->sd_cow, cow_path);
		if(ret) goto tracer_setup_cow_error;
	}else{
		if(!cache_size) dev->sd_cache_size = COW_MAX_MEMORY_DEFAULT;
		else dev->sd_cache_size = cache_size;
		
		if(open_method == 0){
			//calculate how much space should be allocated to the cow file 
			if(!fallocated_space){
				max_file_size = size * KERNEL_SECTOR_SIZE * COW_FALLOCATE_PERCENTAGE_DEFAULT;
				do_div(max_file_size, 100);
				dev->sd_falloc_size = max_file_size;
				do_div(dev->sd_falloc_size, (1024 * 1024));
			}else{
				max_file_size = fallocated_space * (1024 * 1024);
				dev->sd_falloc_size = fallocated_space;
			}
			
			//create and open the cow manager
			LOG_DEBUG("creating cow manager");
			ret = cow_init(cow_path, SECTOR_TO_BLOCK(size), COW_SECTION_SIZE, dev->sd_cache_size, max_file_size, &dev->sd_cow);
			if(ret)	goto tracer_setup_cow_error;
		}else{
			//reload the cow manager
			LOG_DEBUG("reloading cow manager");
			ret = cow_reload(cow_path, SECTOR_TO_BLOCK(size), COW_SECTION_SIZE, dev->sd_cache_size, (open_method == 2), &dev->sd_cow);
			if(ret)	goto tracer_setup_cow_error;
			
			dev->sd_falloc_size = dev->sd_cow->file_max;
			do_div(dev->sd_falloc_size, (1024 * 1024));
		}
	}
	
	//verify that file is on block device
	if(!file_is_on_bdev(dev->sd_cow->filp, bdev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "file specified is not on block device specified");
		goto tracer_setup_cow_error;
	}
	
	//find the cow file's inode number
	LOG_DEBUG("finding cow file inode");
	dev->sd_cow_inode = dev->sd_cow->filp->f_path.dentry->d_inode;
	
	return 0;
	
tracer_setup_cow_error:
	LOG_ERROR(ret, "error setting up cow manager");
	__tracer_destroy_cow_free(dev);
	return ret;
}
#define __tracer_setup_cow_new(dev, bdev, cow_path, size, fallocated_space, cache_size) __tracer_setup_cow(dev, bdev, cow_path, size, fallocated_space, cache_size, 0)
#define __tracer_setup_cow_reload_snap(dev, bdev, cow_path, size, cache_size) __tracer_setup_cow(dev, bdev, cow_path, size, 0, cache_size, 1)
#define __tracer_setup_cow_reload_inc(dev, bdev, cow_path, size, cache_size) __tracer_setup_cow(dev, bdev, cow_path, size, 0, cache_size, 2)
#define __tracer_setup_cow_reopen(dev, bdev, cow_path) __tracer_setup_cow(dev, bdev, cow_path, 0, 0, 0, 3)

static void __tracer_copy_cow(struct snap_device *src, struct snap_device *dest){
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

static int __tracer_setup_cow_path(struct snap_device *dev, struct file *cow_file){
	int ret;
	
	//get the pathname of the cow file (relative to the mountpoint)
	LOG_DEBUG("getting relative pathname of cow file");
	ret = dentry_get_relative_pathname(cow_file->f_path.dentry, &dev->sd_cow_path, NULL);
	if(ret) goto tracer_setup_cow_path_error;

	return 0;
	
tracer_setup_cow_path_error:
	LOG_ERROR(ret, "error setting up cow file path");
	__tracer_destroy_cow_path(dev);
	return ret;
}

static void __tracer_copy_cow_path(struct snap_device *src, struct snap_device *dest){
	dest->sd_cow_path = src->sd_cow_path;
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
}

static int __tracer_setup_snap(struct snap_device *dev, unsigned int minor, struct block_device *bdev, sector_t size){
	int ret;
	
	//allocate request queue
	LOG_DEBUG("allocating queue");
	dev->sd_queue = blk_alloc_queue(GFP_KERNEL);
	if(!dev->sd_queue){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating request queue");
		goto tracer_setup_snap_error;
	}
	
	//register request handler
	LOG_DEBUG("setting up make request function");
	blk_queue_make_request(dev->sd_queue, snap_mrf);
	
	//give our request queue the same properties as the base device's
	LOG_DEBUG("setting queue limits");
	blk_set_stacking_limits(&dev->sd_queue->limits);
	bdev_stack_limits(&dev->sd_queue->limits, bdev, 0);
	
	//we don't support request merging. if the underlying device does we can't send it requests that can be merged
	if(bdev_get_queue(bdev)->merge_bvec_fn) blk_queue_max_hw_sectors(dev->sd_queue, PAGE_SIZE >> KERNEL_SECTOR_LOG_SIZE);
	
	//allocate a gendisk struct
	LOG_DEBUG("allocating gendisk");
	dev->sd_gd = alloc_disk(1);
	if(!dev->sd_gd){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating  gendisk");
		goto tracer_setup_snap_error;
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
		goto tracer_setup_snap_error;
	}
	
	return 0;
	
tracer_setup_snap_error:
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
		goto tracer_start_cow_thread_error;
	}
	
	return 0;
	
tracer_start_cow_thread_error:
	LOG_ERROR(ret, "error setting up cow thread");
	__tracer_destroy_cow_thread(dev);
	return ret;
}
#define __tracer_setup_inc_cow_thread(dev, minor)  __tracer_setup_cow_thread(dev, minor, 0)
#define __tracer_setup_snap_cow_thread(dev, minor)  __tracer_setup_cow_thread(dev, minor, 1)

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
}

static void __tracer_setup_tracing_unverified(struct snap_device *dev, unsigned int minor){
	dev->sd_orig_mrf = NULL;
	
	smp_wmb();
	dev->sd_minor = minor;
	snap_devices[minor] = dev;
	smp_wmb();
}


static int __tracer_setup_tracing(struct snap_device *dev, unsigned int minor){
	int ret;
	
	dev->sd_minor = minor;
	
	//get the base block device's make_request_fn
	LOG_DEBUG("getting the base block device's make_request_fn");
	ret = find_orig_mrf(dev->sd_base_dev, &dev->sd_orig_mrf);
	if(ret) goto tracer_setup_tracing_error;
	
	ret = __tracer_transition_tracing(dev, dev->sd_base_dev, tracing_mrf, &snap_devices[minor]);
	if(ret)	goto tracer_setup_tracing_error;
	
	return 0;
	
tracer_setup_tracing_error:
	LOG_ERROR(ret, "error setting up tracing");
	dev->sd_minor = 0;
	dev->sd_orig_mrf = NULL; 
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

static int tracer_setup_active_snap(struct snap_device *dev, unsigned int minor, char *bdev_path, char *cow_path, unsigned long fallocated_space, unsigned long cache_size){
	int ret;
	
	set_bit(SNAPSHOT, &dev->sd_state);
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);
	
	//setup base device
	ret = __tracer_setup_base_dev(dev, bdev_path);
	if(ret)	goto tracer_setup_active_snap_error;
	
	//setup the cow manager
	ret = __tracer_setup_cow_new(dev, dev->sd_base_dev, cow_path, dev->sd_size, fallocated_space, cache_size);
	if(ret)	goto tracer_setup_active_snap_error;
	
	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret)	goto tracer_setup_active_snap_error;
	
	//setup the snapshot values
	ret = __tracer_setup_snap(dev, minor, dev->sd_base_dev, dev->sd_size);
	if(ret)	goto tracer_setup_active_snap_error;
	
	//setup the cow thread and run it
	ret = __tracer_setup_snap_cow_thread(dev, minor);
	if(ret)	goto tracer_setup_active_snap_error;

	wake_up_process(dev->sd_cow_thread);
	
	//inject the tracing function
	ret = __tracer_setup_tracing(dev, minor);
	if(ret)	goto tracer_setup_active_snap_error;
	
	return 0;
	
tracer_setup_active_snap_error:
	LOG_ERROR(ret, "error setting up tracer as active snapshot");
	tracer_destroy(dev);
	return ret;
}

static int __tracer_setup_unverified(struct snap_device *dev, unsigned int minor, char *bdev_path, char *cow_path, unsigned long cache_size, int is_snap){
	int ret;
	
	if(is_snap) set_bit(SNAPSHOT, &dev->sd_state);
	else clear_bit(SNAPSHOT, &dev->sd_state);
	clear_bit(ACTIVE, &dev->sd_state);
	set_bit(UNVERIFIED, &dev->sd_state);
	
	dev->sd_cache_size = cache_size;
	
	//copy the bdev_path
	ret = string_copy(bdev_path, &dev->sd_bdev_path);
	if(ret) goto tracer_setup_unverified_error;
	
	//copy the cow_path
	ret = string_copy(cow_path, &dev->sd_cow_path);
	if(ret) goto tracer_setup_unverified_error;
	
	__tracer_setup_tracing_unverified(dev, minor);
	
	return 0;
	
tracer_setup_unverified_error:
	LOG_ERROR(ret, "error setting up unverified tracer");
	tracer_destroy(dev);
	return ret;
}
#define tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path, cache_size) __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, 0)
#define tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path, cache_size) __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, 1)

/************************IOCTL TRANSITION FUNCTIONS************************/

static int tracer_active_snap_to_inc(struct snap_device *old_dev){
	int ret;
	struct snap_device *dev;
	
	//allocate new tracer
	ret = tracer_alloc(&dev);
	if(ret) return ret;
	
	clear_bit(SNAPSHOT, &dev->sd_state);
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);
	
	//copy / set fields we need
	__tracer_copy_base_dev(old_dev, dev);
	__tracer_copy_cow_path(old_dev, dev);
	
	//setup the cow thread
	ret = __tracer_setup_inc_cow_thread(dev, old_dev->sd_minor);
	if(ret) goto tracer_transition_inc_error;
	
	//inject the tracing function
	ret = __tracer_setup_tracing(dev, old_dev->sd_minor);
	if(ret)	goto tracer_transition_inc_error;
	
	//stop the old cow thread
	__tracer_destroy_cow_thread(old_dev);
	
	//sanity check to ensure no errors have occurred while cleaning up cow thread
	ret = tracer_read_fail_state(old_dev);
	if(ret){
		LOG_ERROR(ret, "errors occurred while cleaning up cow thread, putting incremental into error state");
		tracer_set_fail_state(dev, ret);
		__tracer_destroy_snap(old_dev);
		kfree(old_dev);

		return ret;
	}

	//copy cow manager to new device, must be done after destroying cow thread to prevent double ownership
	__tracer_copy_cow(old_dev, dev);

	//wake up new cow thread
	wake_up_process(dev->sd_cow_thread);

	//truncate the cow file
	ret = cow_truncate_to_index(dev->sd_cow);
	if(ret){
		//not a critical error, we can just print a warning
		LOG_WARN("warning: cow file truncation failed, incremental will use more disk space than needed");
	}
	
	//destroy the unneeded fields of the old_dev and the old_dev itself
	__tracer_destroy_snap(old_dev);
	kfree(old_dev);
	
	return 0;
	
tracer_transition_inc_error:
	LOG_ERROR(ret, "error transitioning to incremental mode");
	__tracer_destroy_cow_thread(dev);
	kfree(dev);
	
	return ret;
}

static int tracer_active_inc_to_snap(struct snap_device *old_dev, char *cow_path, unsigned long fallocated_space){
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
	ret = __tracer_setup_cow_new(dev, dev->sd_base_dev, cow_path, dev->sd_size, fallocated_space, dev->sd_cache_size);
	if(ret) goto tracer_active_inc_to_snap_error;
	
	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret) goto tracer_active_inc_to_snap_error;
	
	//setup the snapshot values
	ret = __tracer_setup_snap(dev, old_dev->sd_minor, dev->sd_base_dev, dev->sd_size);
	if(ret) goto tracer_active_inc_to_snap_error;
	
	//setup the cow thread
	ret = __tracer_setup_snap_cow_thread(dev, old_dev->sd_minor);
	if(ret) goto tracer_active_inc_to_snap_error;
	
	//start tracing (overwrites old_dev's tracing)
	ret = __tracer_setup_tracing(dev, old_dev->sd_minor);
	if(ret) goto tracer_active_inc_to_snap_error;
	
	//stop the old cow thread and start the new one
	__tracer_destroy_cow_thread(old_dev);
	wake_up_process(dev->sd_cow_thread);
	
	//destroy the unneeded fields of the old_dev and the old_dev itself
	__tracer_destroy_cow_path(old_dev);
	__tracer_destroy_cow_sync_and_free(old_dev);
	kfree(old_dev);
	
	return 0;
	
tracer_active_inc_to_snap_error:
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
	if(!cache_size) cache_size = COW_MAX_MEMORY_DEFAULT;
	if(test_bit(ACTIVE, &dev->sd_state)) cow_modify_cache_size(dev->sd_cow, cache_size);
}

/************************IOCTL HANDLER FUNCTIONS************************/

static int __verify_minor(unsigned int minor, int in_use){
	//check minor number is within range
	if(minor >= MAX_SNAP_DEVICES){
		LOG_ERROR(-EINVAL, "minor number specified is out of range");
		return -EINVAL;
	}
	
	//check if the device is in use
	if(!in_use){
		if(snap_devices[minor]){
			LOG_ERROR(-EBUSY, "device specified already exists");
			return -EBUSY;
		}
	}else{
		if(!snap_devices[minor]){
			LOG_ERROR(-EINVAL, "device specified does not exist");
			return -EINVAL;
		}
		
		//check that the device is not busy
		if(snap_devices[minor]->sd_refs){
			LOG_ERROR(-EINVAL, "device specified is busy");
			return -EINVAL;
		}		
	}
	
	return 0;
}
#define verify_minor_available(minor) __verify_minor(minor, 0)
#define verify_minor_in_use(minor) __verify_minor(minor, 1)

static int __verify_bdev_writable(char *bdev_path, int *out){
	int ret = 0;
	struct block_device *bdev;
	
	//open the base block device
	bdev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);
	if(IS_ERR(bdev)){
		bdev = NULL;
		*out = 0;
		ret = 0;
	}else if(!bdev->bd_super || (bdev->bd_super->s_flags & MS_RDONLY)){
		*out = 0;
		ret = 0;
	}else{
		*out = 1;
		ret = 0;
	}
	
	if(bdev) bdev_put(bdev);
	return ret;
}

static int __ioctl_setup(unsigned int minor, char *bdev_path, char *cow_path, unsigned long fallocated_space, unsigned long cache_size, int is_snap, int is_reload){
	int ret, is_mounted;
	struct snap_device *dev = NULL;
	
	LOG_DEBUG("received %s %s ioctl - %u : %s : %s", (is_reload)? "reload" : "setup", (is_snap)? "snap" : "inc", minor, bdev_path, cow_path);
	
	//verify that the minor number is valid
	ret = verify_minor_available(minor);
	if(ret) goto ioctl_setup_error;
	
	//check if block device is mounted
	ret = __verify_bdev_writable(bdev_path, &is_mounted);
	if(ret) goto ioctl_setup_error;
	
	//check that reload / setup command matches current mount state
	if(is_mounted && is_reload){
		ret = -EINVAL;
		LOG_ERROR(ret, "illegal to perform reload while mounted");
		goto ioctl_setup_error;
	}else if(!is_mounted && !is_reload){
		ret = -EINVAL;
		LOG_ERROR(ret, "illegal to perform setup while unmounted");
		goto ioctl_setup_error;
	}
	
	//allocate the tracing struct
	ret = tracer_alloc(&dev);
	if(ret) goto ioctl_setup_error;
	
	//route to the appropriate setup function
	if(is_snap){
		if(is_mounted) ret = tracer_setup_active_snap(dev, minor, bdev_path, cow_path, fallocated_space, cache_size);
		else ret = tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path, cache_size);
	}else{
		if(!is_mounted) ret = tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path, cache_size);
		else{
			ret = -EINVAL;
			LOG_ERROR(ret, "illegal to setup as active incremental");
			goto ioctl_setup_error;
		}
	}
	
	if(ret) goto ioctl_setup_error;
	
	return 0;
	
ioctl_setup_error:
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
	ret = verify_minor_in_use(minor);
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
	ret = verify_minor_in_use(minor);
	if(ret) goto ioctl_transition_inc_error;
	
	dev = snap_devices[minor];
	
	//check that the device is not in the fail state
	if(tracer_read_fail_state(dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is in the fail state");
		goto ioctl_transition_inc_error;
	}
	
	//check that tracer is in active snapshot state
	if(!test_bit(SNAPSHOT, &dev->sd_state) || !test_bit(ACTIVE, &dev->sd_state)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is not in active snapshot mode");
		goto ioctl_transition_inc_error;
	}
	
	ret = tracer_active_snap_to_inc(dev);
	if(ret) goto ioctl_transition_inc_error;
	
	return 0;
	
ioctl_transition_inc_error:
	LOG_ERROR(ret, "error during transition to incremental ioctl handler");
	return ret;
}

static int ioctl_transition_snap(unsigned int minor, char *cow_path, unsigned long fallocated_space){
	int ret;
	struct snap_device *dev;
	
	LOG_DEBUG("received transition snap ioctl - %u : %s", minor, cow_path);
	
	//verify that the minor number is valid
	ret = verify_minor_in_use(minor);
	if(ret) goto ioctl_transition_snap_error;
	
	dev = snap_devices[minor];
	
	//check that the device is not in the fail state
	if(tracer_read_fail_state(dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is in the fail state");
		goto ioctl_transition_snap_error;
	}
	
	//check that tracer is in active incremental state
	if(test_bit(SNAPSHOT, &dev->sd_state) || !test_bit(ACTIVE, &dev->sd_state)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is not in active incremental mode");
		goto ioctl_transition_snap_error;
	}
	
	ret = tracer_active_inc_to_snap(dev, cow_path, fallocated_space);
	if(ret) goto ioctl_transition_snap_error;
	
	return 0;
	
ioctl_transition_snap_error:
	LOG_ERROR(ret, "error during transition to snapshot ioctl handler");
	return ret;
}

static int ioctl_reconfigure(unsigned int minor, unsigned long cache_size){
	int ret;
	struct snap_device *dev;
	
	LOG_DEBUG("received reconfigure ioctl - %u : %lu", minor, cache_size);
	
	//verify that the minor number is valid
	ret = verify_minor_in_use(minor);
	if(ret) goto ioctl_reconfigure_error;
	
	dev = snap_devices[minor];
	
	//check that the device is not in the fail state
	if(tracer_read_fail_state(dev)){
		ret = -EINVAL;
		LOG_ERROR(ret, "device specified is in the fail state");
		goto ioctl_reconfigure_error;
	}
	
	tracer_reconfigure(dev, cache_size);
	
	return 0;
	
ioctl_reconfigure_error:
	LOG_ERROR(ret, "error during reconfigure ioctl handler");
	return ret;
}

static long ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	int ret;
	char *bdev_path = NULL;
	char *cow_path = NULL;
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
	default:
		ret = -EINVAL;
		LOG_ERROR(ret, "invalid ioctl called");
		break;
	}
	mutex_unlock(&ioctl_mutex);
	
	if(bdev_path) kfree(bdev_path);
	if(cow_path) kfree(cow_path);
	
	return ret;
}

/************************AUTOMATIC TRANSITION FUNCTIONS************************/

static void __tracer_active_to_dormant(struct snap_device *dev){
	int ret;
	
	//stop the cow thread
	__tracer_destroy_cow_thread(dev);
	
	//close the cow manager
	ret = __tracer_destroy_cow_sync_and_close(dev);
	if(ret) goto tracer_transition_dormant_error;
	
	//mark as dormant
	smp_wmb();
	clear_bit(ACTIVE, &dev->sd_state);
	
	return;
	
tracer_transition_dormant_error:
	LOG_ERROR(ret, "error transitioning tracer to dormant state");
	tracer_set_fail_state(dev, ret);
}

static void __tracer_unverified_snap_to_active(struct snap_device *dev, char __user *user_mount_path){
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
	if(ret)	goto tracer_unverified_snap_to_active_error;
	
	//generate the full pathname
	ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
	if(ret) goto tracer_unverified_snap_to_active_error;
	
	//setup the cow manager
	ret = __tracer_setup_cow_reload_snap(dev, dev->sd_base_dev, cow_path, dev->sd_size, dev->sd_cache_size);
	if(ret)	goto tracer_unverified_snap_to_active_error;
	
	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret)	goto tracer_unverified_snap_to_active_error;
	
	//setup the snapshot values
	ret = __tracer_setup_snap(dev, minor, dev->sd_base_dev, dev->sd_size);
	if(ret)	goto tracer_unverified_snap_to_active_error;
	
	//setup the cow thread and run it
	ret = __tracer_setup_snap_cow_thread(dev, minor);
	if(ret)	goto tracer_unverified_snap_to_active_error;

	wake_up_process(dev->sd_cow_thread);
	
	//inject the tracing function
	ret = __tracer_setup_tracing(dev, minor);
	if(ret)	goto tracer_unverified_snap_to_active_error;
	
	kfree(bdev_path);
	kfree(rel_path);
	kfree(cow_path);
	
	return;
	
tracer_unverified_snap_to_active_error:
	LOG_ERROR(ret, "error transitioning snapshot tracer to active state");
	tracer_destroy(dev);
	tracer_setup_unverified_snap(dev, minor, bdev_path, rel_path, cache_size);
	tracer_set_fail_state(dev, ret);
	kfree(bdev_path);
	kfree(rel_path);
	if(cow_path) kfree(cow_path);
}

static void __tracer_unverified_inc_to_active(struct snap_device *dev, char __user *user_mount_path){
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
	if(ret)	goto tracer_unverified_inc_to_active_error;

	//generate the full pathname
	ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
	if(ret) goto tracer_unverified_inc_to_active_error;
	
	//setup the cow manager
	ret = __tracer_setup_cow_reload_inc(dev, dev->sd_base_dev, cow_path, dev->sd_size, dev->sd_cache_size);
	if(ret)	goto tracer_unverified_inc_to_active_error;
		
	//setup the cow path
	ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
	if(ret)	goto tracer_unverified_inc_to_active_error;
		
	//setup the cow thread and run it
	ret = __tracer_setup_inc_cow_thread(dev, minor);
	if(ret)	goto tracer_unverified_inc_to_active_error;

	wake_up_process(dev->sd_cow_thread);
		
	//inject the tracing function
	ret = __tracer_setup_tracing(dev, minor);
	if(ret)	goto tracer_unverified_inc_to_active_error;
	
	kfree(bdev_path);
	kfree(rel_path);
	kfree(cow_path);
	
	return;
	
tracer_unverified_inc_to_active_error:
	LOG_ERROR(ret, "error transitioning incremental to active state");
	tracer_destroy(dev);
	tracer_setup_unverified_inc(dev, minor, bdev_path, rel_path, cache_size);
	tracer_set_fail_state(dev, ret);
	kfree(bdev_path);
	kfree(rel_path);
	if(cow_path) kfree(cow_path);
}

static void __tracer_dormant_to_active(struct snap_device *dev, char __user *user_mount_path){
	int ret;
	char *cow_path;
	
	//generate the full pathname
	ret = user_mount_pathname_concat(user_mount_path, dev->sd_cow_path, &cow_path);
	if(ret) goto tracer_dormant_to_active_error;
	
	//setup the cow manager
	ret = __tracer_setup_cow_reopen(dev, dev->sd_base_dev, cow_path);
	if(ret)	goto tracer_dormant_to_active_error;
	
	//restart the cow thread
	if(test_bit(SNAPSHOT, &dev->sd_state)) ret = __tracer_setup_snap_cow_thread(dev, dev->sd_minor);
	else ret = __tracer_setup_inc_cow_thread(dev, dev->sd_minor);
	
	if(ret) goto tracer_dormant_to_active_error;
	
	wake_up_process(dev->sd_cow_thread);
	
	//set the state to active
	smp_wmb();
	set_bit(ACTIVE, &dev->sd_state);
	clear_bit(UNVERIFIED, &dev->sd_state);
	
	kfree(cow_path);
	
	return;
	
tracer_dormant_to_active_error:
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

static void auto_transition_active(unsigned int i, char __user *dir_name){
	struct snap_device *dev = snap_devices[i];
	
	mutex_lock(&ioctl_mutex);
	
	if(test_bit(UNVERIFIED, &dev->sd_state)){
		if(test_bit(SNAPSHOT, &dev->sd_state)) __tracer_unverified_snap_to_active(dev, dir_name);
		else __tracer_unverified_inc_to_active(dev, dir_name);
	}else __tracer_dormant_to_active(dev, dir_name);
	
	mutex_unlock(&ioctl_mutex);
}

/***************************SYSTEM CALL HOOKING***************************/

static int __handle_bdev_mount_nowrite(struct vfsmount *mnt, unsigned int *idx_out){
	int ret;
	unsigned int i; 
	struct snap_device *dev;
	
	tracer_for_each(dev, i){
		if(!dev || !test_bit(ACTIVE, &dev->sd_state) || tracer_read_fail_state(dev) || dev->sd_base_dev != mnt->mnt_sb->s_bdev) continue;
		
		//if we are unmounting the vfsmount we are using go to dormant state
		if(mnt == dev->sd_cow->filp->f_path.mnt){
			LOG_DEBUG("block device umount detected for device %d", i);
			auto_transition_dormant(i);
			
			ret = 0;
			goto handle_bdev_mount_nowrite_out;
		}
	}
	i = 0;
	ret = -ENODEV;
	
handle_bdev_mount_nowrite_out:
	*idx_out = i;
	return ret;
}

static int __handle_bdev_mount_writable(char __user *dir_name, struct block_device *bdev, unsigned int *idx_out){
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
				bdev_put(cur_bdev);
				
				ret = 0;
				goto handle_bdev_mount_writable_out;
			}
				
			//put the block device
			bdev_put(cur_bdev);
			
		}else if(dev->sd_base_dev == bdev){
			LOG_DEBUG("block device mount detected for dormant device %d", i);
			auto_transition_active(i, dir_name);
			
			ret = 0;
			goto handle_bdev_mount_writable_out;
		}
	}
	i = 0;
	ret = -ENODEV;
	
handle_bdev_mount_writable_out:
	*idx_out = i;
	return ret;
}

static int handle_bdev_mount_event(char __user *dir_name, int follow_flags, unsigned int *idx_out, int mount_writable){
	int ret, lookup_flags = 0;
	char *pathname = NULL;
	struct path path = {};
	struct block_device *bdev;
	
	if(!(follow_flags & UMOUNT_NOFOLLOW)) lookup_flags |= LOOKUP_FOLLOW;
	
	ret = user_path_at(AT_FDCWD, dir_name, lookup_flags, &path);
	if(ret){
		//error finding path
		goto handle_bdev_mount_event_out;
	}else if(path.dentry != path.mnt->mnt_root){
		//path specified isn't a mount point
		ret = -ENODEV;
		goto handle_bdev_mount_event_out;
	}
	
	bdev = path.mnt->mnt_sb->s_bdev;
	if(!bdev){
		//path specified isn't mounted on a block device
		ret = -ENODEV;
		goto handle_bdev_mount_event_out;
	}
	
	if(!mount_writable) ret = __handle_bdev_mount_nowrite(path.mnt, idx_out);
	else ret = __handle_bdev_mount_writable(dir_name, bdev, idx_out);
	if(ret){
		//no block device found that matched an incremental
		goto handle_bdev_mount_event_out;
	}
	
	kfree(pathname);
	path_put(&path);
	return 0;

handle_bdev_mount_event_out:
	if(pathname) kfree(pathname);
	path_put(&path);
	
	*idx_out = 0;
	return ret;
}
#define handle_bdev_mount_nowrite(dir_name, follow_flags, idx_out) handle_bdev_mount_event(dir_name, follow_flags, idx_out, 0)
#define handle_bdev_mounted_writable(dir_name, idx_out) handle_bdev_mount_event(dir_name, 0, idx_out, 1)

static void post_umount_check(int dormant_ret, long umount_ret, unsigned int idx, char __user *dir_name){
	struct snap_device *dev;
	struct super_block *sb;
	
	//if we didn't do anything or failed, just return
	if(dormant_ret) return;
	
	//if we successfully went dormant, but the umount call failed, reactivate
	if(umount_ret){
		LOG_DEBUG("umount call failed, reactivating tracer %u", idx);
		auto_transition_active(idx, dir_name);
		return;
	}
	
	//force the umount operation to complete synchronously
	task_work_flush();
	
	//if we went dormant, but the block device is still mounted somewhere, goto fail state
	dev = snap_devices[idx];
	sb = get_super(dev->sd_base_dev);
	if(sb){
		LOG_ERROR(-EIO, "device still mounted after umounting cow file's file-system. entering error state");
		tracer_set_fail_state(dev, -EIO);
		drop_super(sb);
		return;
	}
	
	LOG_DEBUG("post umount check succeeded");
}

static asmlinkage long mount_hook(char __user *dev_name, char __user *dir_name, char __user *type, unsigned long flags, void __user *data){
	int ret;
	long sys_ret;
	unsigned int idx;
	unsigned long real_flags = flags;
	
	//get rid of the magic value if its present
	if((real_flags & MS_MGC_MSK) == MS_MGC_VAL) real_flags &= ~MS_MGC_MSK;
	
	LOG_DEBUG("detected block device mount: %s -> %s : %lu", dev_name, dir_name, real_flags);

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
	
	LOG_DEBUG("detected block device umount: %s : %d", name, flags);
	
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

static inline void disable_page_protection(unsigned long *cr0) {
	*cr0 = read_cr0();
	write_cr0(*cr0 & ~CR0_WP);
}

static inline void reenable_page_protection(unsigned long *cr0) {
    write_cr0(*cr0);
}

static void **find_sys_call_table(void){
	void **sct;
	
	if(!SYS_MOUNT_ADDR || !SYS_UMOUNT_ADDR) return NULL;

	for(sct = (void **)PAGE_OFFSET; sct < (void **)ULONG_MAX; sct++){
		if(sct[__NR_mount] != (void *)SYS_MOUNT_ADDR) continue;
		if(sct[__NR_umount2] != (void *)SYS_UMOUNT_ADDR) continue;
#ifdef HAVE_SYS_OLDUMOUNT
		if(sct[__NR_umount] != (void *)SYS_OLDUMOUNT_ADDR) continue;
#endif
		return sct;
	}
	
	return NULL;
}

#define set_syscall(sys_nr, orig_call_save, new_call) 		\
	orig_call_save = system_call_table[sys_nr];				\
	system_call_table[sys_nr] = new_call;
	
#define restore_syscall(sys_nr, orig_call_save) system_call_table[sys_nr] = orig_call_save;

/***************************BLOCK DEVICE DRIVER***************************/

static int __tracer_add_ref(struct snap_device *dev, int ref_cnt){
	int ret = 0;;
	
	if(!dev){
		ret = -EFAULT;
		LOG_ERROR(ret, "requested snapshot device does not exist");
		goto tracer_add_ref_error;
	}
	
	mutex_lock(&ioctl_mutex);
	dev->sd_refs += ref_cnt;
	mutex_unlock(&ioctl_mutex);

tracer_add_ref_error:
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

static int info_proc_show(struct seq_file *m, void *v){
	int i, error;
	int add_comma = 0;
	struct snap_device *dev;
	
	mutex_lock(&ioctl_mutex);
	
	seq_printf(m, "{\n");
	seq_printf(m, "\t\"version\": \"%s\",\n", VERSION_STRING);
	seq_printf(m, "\t\"devices\": [\n");
	tracer_for_each(dev, i){
		if(!dev) continue;
		
		if(!add_comma) add_comma = 1;
		else seq_printf(m, ",\n");
		seq_printf(m, "\t\t{\n");
		seq_printf(m, "\t\t\t\"minor\": %u,\n", i);
		seq_printf(m, "\t\t\t\"cow_file\": \"%s\",\n", dev->sd_cow_path);
		seq_printf(m, "\t\t\t\"block_device\": \"%s\",\n", dev->sd_bdev_path);
		
		seq_printf(m, "\t\t\t\"max_cache\": %lu,\n", (dev->sd_cache_size)? dev->sd_cache_size : COW_MAX_MEMORY_DEFAULT);
		if(!test_bit(UNVERIFIED, &dev->sd_state)) seq_printf(m, "\t\t\t\"fallocate\": %llu,\n", ((unsigned long long)dev->sd_falloc_size) * 1024 * 1024);
		
		error = tracer_read_fail_state(dev);
		if(error) seq_printf(m, "\t\t\t\"error\": %d,\n", error);
		
		seq_printf(m, "\t\t\t\"state\": %lu\n", dev->sd_state);
		seq_printf(m, "\t\t}");
	}
	seq_printf(m, "\n\t]\n");
	seq_printf(m, "}\n");
	
	mutex_unlock(&ioctl_mutex);
	
	return 0;
}

static int info_proc_open(struct inode *inode, struct file *filp){
	return single_open(filp, info_proc_show, NULL);
}

/************************MODULE SETUP AND DESTROY************************/

static void agent_exit(void){
	int i;
	struct snap_device *dev;
	unsigned long cr0;

	LOG_DEBUG("module exit");
	
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
			LOG_DEBUG("snap - %p", dev);
			if(dev) tracer_destroy(dev);
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
	unsigned long cr0;
	
	LOG_DEBUG("module init");
	
	//init ioctl mutex
	mutex_init(&ioctl_mutex);

	//get a major number for the driver
	LOG_DEBUG("get major number");
	major = register_blkdev(0, DRIVER_NAME);
	if(major <= 0){
		ret = -EBUSY;
		LOG_ERROR(ret, "error requesting major number from the kernel");
		goto init_error;
	}
	
	//allocate global device array
	LOG_DEBUG("allocate global device array");
	snap_devices = kzalloc(MAX_SNAP_DEVICES * sizeof(struct snap_device*), GFP_KERNEL);
	if(!snap_devices){
		ret = -ENOMEM;
		LOG_ERROR(ret, "error allocating global device array");
		goto init_error;
	}
	
	//register proc file
	LOG_DEBUG("registering proc file");
	info_proc = proc_create(INFO_PROC_FILE, 0, NULL, &info_proc_fops);
	if(!info_proc){
		ret = -ENOENT;
		LOG_ERROR(ret, "error registering proc file");
		goto init_error;
	}
	
	//register control device
	LOG_DEBUG("registering control device");
	ret = misc_register(&snap_control_device);
	if(ret){
		LOG_ERROR(ret, "error registering control device");
		goto init_error;
	}
	
	if(MAY_HOOK_SYSCALLS){
		//find sys_call_table
		LOG_DEBUG("locating system call table");
		system_call_table = find_sys_call_table();
		if(!system_call_table){
			LOG_WARN("failed to locate system call table, persistence disabled");
			return 0;
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
	}
	
	return 0;
	
init_error:
	agent_exit();
	return ret;
}
module_init(agent_init);
