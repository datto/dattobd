/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#ifndef LIBDATTOBD_H_
#define LIBDATTOBD_H_

#include "../src/dattobd.h"

#ifdef __cplusplus
extern "C" {
#endif

int dattobd_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size);

int dattobd_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int dattobd_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int dattobd_destroy(unsigned int minor);

int dattobd_transition_incremental(unsigned int minor);

int dattobd_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space);

int dattobd_reconfigure(unsigned int minor, unsigned long cache_size);

int dattobd_info(unsigned int minor, struct dattobd_info *info);

int dattobd_version(struct dattobd_version *ver);

int dattobd_active_device_info(struct dattobd_active_device_info *info);

int dattobd_generic(unsigned long iocid, void *data);


#ifdef __cplusplus
}
#endif

#endif /* LIBDATTOBD_H_ */
