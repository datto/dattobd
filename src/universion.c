/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include <universion.h>

#ifndef HAVE_BLKDEV_GET_BY_PATH
struct block_device *blkdev_get_by_path(const char *path, fmode_t mode, void *holder){
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
void submit_bio_wait_endio(struct bio *bio, int error){
	struct submit_bio_ret *ret = bio->bi_private;
	ret->error = error;
	complete(&ret->event);
}

int submit_bio_wait(int rw, struct bio *bio){
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

#ifndef HAVE_KERN_PATH
int kern_path(const char *name, unsigned int flags, struct path *path){
	struct nameidata nd;
	int ret = path_lookup(name, flags, &nd);
	if(!ret) *path = nd.path;
	return ret;
}
#endif

//reimplementation of task_work_run() to force fput() and mntput() to perform their work synchronously
#ifdef HAVE_TASK_STRUCT_TASK_WORKS_HLIST
void task_work_flush(void){
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
void task_work_flush(void){
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
#endif

int copy_string_from_user(const char __user *data, char **out_ptr){
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
