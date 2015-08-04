/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "libdattobd.h"

#ifdef __cplusplus
extern "C" {
#endif

int setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size){
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

int reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
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

int reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size){
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

int destroy(unsigned int minor){
	int fd, ret;
	
	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;
	
	ret = ioctl(fd, IOCTL_DESTROY, &minor);
	
	close(fd);
	return ret;
}

int transition_incremental(unsigned int minor){
	int fd, ret;
	
	fd = open("/dev/datto-ctl", O_RDONLY);
	if(fd < 0) return -1;
	
	ret = ioctl(fd, IOCTL_TRANSITION_INC, &minor);
	
	close(fd);
	return ret;
}

int transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space){
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

int reconfigure(unsigned int minor, unsigned long cache_size){
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

#ifdef __cplusplus
}
#endif
