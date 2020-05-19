// SPDX-License-Identifier: LGPL-2.1-or-later

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "libelastio-snap.h"

int elastio_snap_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size){
	int fd, ret;
	struct setup_params sp;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
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

int elastio_snap_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
	int fd, ret;
	struct reload_params rp;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	rp.minor = minor;
	rp.bdev = bdev;
	rp.cow = cow;
	rp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_RELOAD_SNAP, &rp);

	close(fd);
	return ret;
}

int elastio_snap_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
	int fd, ret;
	struct reload_params rp;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	rp.minor = minor;
	rp.bdev = bdev;
	rp.cow = cow;
	rp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_RELOAD_INC, &rp);

	close(fd);
	return ret;
}

int elastio_snap_destroy(unsigned int minor){
	int fd, ret;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_DESTROY, &minor);

	close(fd);
	return ret;
}

int elastio_snap_transition_incremental(unsigned int minor){
	int fd, ret;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_TRANSITION_INC, &minor);

	close(fd);
	return ret;
}

int elastio_snap_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space){
	int fd, ret;
	struct transition_snap_params tp;

	tp.minor = minor;
	tp.cow = cow;
	tp.fallocated_space = fallocated_space;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_TRANSITION_SNAP, &tp);

	close(fd);
	return ret;
}

int elastio_snap_reconfigure(unsigned int minor, unsigned long cache_size){
	int fd, ret;
	struct reconfigure_params rp;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	rp.minor = minor;
	rp.cache_size = cache_size;

	ret = ioctl(fd, IOCTL_RECONFIGURE, &rp);

	close(fd);
	return ret;
}

int elastio_snap_info(unsigned int minor, struct elastio_snap_info *info){
	int fd, ret;

	if(!info){
		errno = EINVAL;
		return -1;
	}

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	info->minor = minor;

	ret = ioctl(fd, IOCTL_ASSURIO_SNAP_INFO, info);

	close(fd);
	return ret;
}

int elastio_snap_get_free_minor(void){
	int fd, ret, minor;

	fd = open("/dev/elastio-snap-ctl", O_RDONLY);
	if(fd < 0) return -1;

	ret = ioctl(fd, IOCTL_GET_FREE, &minor);

	close(fd);

	if(!ret) return minor;
	return ret;
}
