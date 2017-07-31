/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "libdattobd.h"

int dattobd_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size){
	int fd, ret;
	struct setup_params sp;

	sp.minor = minor;
	sp.bdev = bdev;
	sp.cow = cow;
	sp.fallocated_space = fallocated_space;
	sp.cache_size = cache_size;

	return dattobd_generic(IOCTL_SETUP_SNAP, &sp);
}

int dattobd_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
	struct reload_params rp;

	rp.minor = minor;
	rp.bdev = bdev;
	rp.cow = cow;
	rp.cache_size = cache_size;

	return dattobd_generic(IOCTL_RELOAD_SNAP, &rp);
}

int dattobd_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
	struct reload_params rp;

	rp.minor = minor;
	rp.bdev = bdev;
	rp.cow = cow;
	rp.cache_size = cache_size;

	return dattobd_generic(IOCTL_RELOAD_INC, &rp);
}

int dattobd_destroy(unsigned int minor){

	return dattobd_generic(IOCTL_DESTROY, &minor);
}

int dattobd_transition_incremental(unsigned int minor){

	return dattobd_generic(IOCTL_TRANSITION_INC, &minor);
}

int dattobd_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space){
	struct transition_snap_params tp;

	tp.minor = minor;
	tp.cow = cow;
	tp.fallocated_space = fallocated_space;

	return dattobd_generic(IOCTL_TRANSITION_SNAP, &tp);
}

int dattobd_reconfigure(unsigned int minor, unsigned long cache_size){
	struct reconfigure_params rp;

	rp.minor = minor;
	rp.cache_size = cache_size;

	return dattobd_generic(IOCTL_RECONFIGURE, &rp);
}

int dattobd_info(unsigned int minor, struct dattobd_info *info){

	if(!info){
		errno = EINVAL;
		return -1;
	}

	info->minor = minor;

	return dattobd_generic(IOCTL_DATTOBD_INFO, info);
}

int dattobd_version(struct dattobd_version *ver) {

    if(!ver){
        errno = EINVAL;
        return -1;
    }

    return dattobd_generic(IOCTL_DATTOBD_VERSION, ver);
}

int dattobd_all_device_info(struct dattobd_all_device_info *info)
{
    // on entry info->count has the number of info structs allocated that it is safe for us to return
    // the actually memory allocated by the caller must be at least
    // sizeof(dattobd_all_device_info) + ((info->count - 1) * sizeof(dattobd_info))
    if(!info){
        errno = EINVAL;
        return -1;
    }

    return dattobd_generic(IOCTL_DATTOBD_ALL_DEVICE_INFO, info);
}

int dattobd_generic(unsigned long iocid, void *data)
{
    int fd, ret;

    fd = open("/dev/datto-ctl", O_RDONLY);
    if(fd < 0) return -1;

    ret = ioctl(fd, iocid, data);

    close(fd);
    return ret;

}


