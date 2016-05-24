/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "universion.h"

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
