/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#ifndef DATTOBD_H_
#define DATTOBD_H_

#include <linux/ioctl.h>

#define DATTO_IOCTL_MAGIC 0x91

struct setup_params{
	char *bdev; //name of block device to snapshot
	char *cow; //name of cow file for snapshot
	unsigned long fallocated_space; //space allocated to the cow file (in megabytes)
	unsigned long cache_size; //maximum cache size (in bytes)
	unsigned int minor; //requested minor number of the device
};

struct reload_params{
	char *bdev; //name of block device to snapshot
	char *cow; //name of cow file for snapshot
	unsigned long cache_size; //maximum cache size (in bytes)
	unsigned int minor; //requested minor number of the device
};

struct transition_snap_params{
	char *cow; //name of cow file for snapshot
	unsigned long fallocated_space; //space allocated to the cow file (in bytes)
	unsigned int minor; //requested minor
};

struct reconfigure_params{
	unsigned long cache_size; //maximum cache size (in bytes)
	unsigned int minor; //requested minor number of the device
};

#define IOCTL_SETUP_SNAP _IOW(DATTO_IOCTL_MAGIC, 1, struct setup_params) //in: see above
#define IOCTL_RELOAD_SNAP _IOW(DATTO_IOCTL_MAGIC, 2, struct reload_params) //in: see above
#define IOCTL_RELOAD_INC _IOW(DATTO_IOCTL_MAGIC, 3, struct reload_params) //in: see above
#define IOCTL_DESTROY _IOW(DATTO_IOCTL_MAGIC, 4, unsigned int) //in: minor
#define IOCTL_TRANSITION_INC _IOW(DATTO_IOCTL_MAGIC, 5, unsigned int) //in: minor
#define IOCTL_TRANSITION_SNAP _IOW(DATTO_IOCTL_MAGIC, 6, struct transition_snap_params) //in: see above
#define IOCTL_RECONFIGURE _IOW(DATTO_IOCTL_MAGIC, 7, struct reconfigure_params) //in: see above

#endif /* DATTOBD_H_ */
