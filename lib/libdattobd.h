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

int setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size);

int reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);

int destroy(unsigned int minor);

int transition_incremental(unsigned int minor);

int transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space);

int reconfigure(unsigned int minor, unsigned long cache_size);

int dattobd_info(unsigned int minor, struct dattobd_info *info);

#ifdef __cplusplus
}
#endif

#endif /* LIBDATTOBD_H_ */
