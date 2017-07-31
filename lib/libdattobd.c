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

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	sp.minor = minor;
	sp.bdev = bdev;
	sp.cow = cow;
	sp.fallocated_space = fallocated_space;
	sp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_SETUP_SNAP, &sp);

	close(fd);
	return ret;
}

int dattobd_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
	int fd, ret;
	struct reload_params rp;

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	rp.minor = minor;
	rp.bdev = bdev;
	rp.cow = cow;
	rp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_RELOAD_SNAP, &rp);

	close(fd);
	return ret;
}

int dattobd_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
	int fd, ret;
	struct reload_params rp;

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	rp.minor = minor;
	rp.bdev = bdev;
	rp.cow = cow;
	rp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_RELOAD_INC, &rp);

	close(fd);
	return ret;
}

int dattobd_destroy(unsigned int minor){
	int fd, ret;

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_DESTROY, &minor);

	close(fd);
	return ret;
}

int dattobd_transition_incremental(unsigned int minor){
	int fd, ret;

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_TRANSITION_INC, &minor);

	close(fd);
	return ret;
}

int dattobd_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space){
	int fd, ret;
	struct transition_snap_params tp;

	tp.minor = minor;
	tp.cow = cow;
	tp.fallocated_space = fallocated_space;

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_TRANSITION_SNAP, &tp);

	close(fd);
	return ret;
}

int dattobd_reconfigure(unsigned int minor, unsigned long cache_size){
	int fd, ret;
	struct reconfigure_params rp;

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	rp.minor = minor;
	rp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_RECONFIGURE, &rp);

	close(fd);
	return ret;
}

int dattobd_info(unsigned int minor, struct dattobd_info *info){
	int fd, ret;

	if(!info){
		errno = EINVAL;
		return -1;
	}

	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;

	info->minor = minor;

	ret = ioctl(fd, IOCTL_DATTOBD_INFO, info);

	close(fd);
	return ret;
}

int dattobd_version(struct dattobd_version *ver) {
    int fd, ret;

    if(!ver){
        errno = EINVAL;
        return -1;
    }

    fd = open("/dev/datto-ctl", O_RDONLY);
    if(fd < 0) return -1;

    ret = ioctl(fd, IOCTL_DATTOBD_VERSION, ver);

    close(fd);
    return ret;
}




