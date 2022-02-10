// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef LOGGING_H_
#define LOGGING_H_

#include "includes.h"

// printing macros
#define LOG_DEBUG(fmt, args...)                                                \
        do {                                                                   \
                if (dattobd_debug)                                             \
                        printk(KERN_DEBUG "datto: " fmt "\n", ##args);         \
        } while (0)

#define LOG_WARN(fmt, args...) printk(KERN_WARNING "datto: " fmt "\n", ##args)
#define LOG_ERROR(error, fmt, args...)                                         \
        printk(KERN_ERR "datto: " fmt ": %d\n", ##args, error)
#define PRINT_BIO(text, bio)                                                   \
        LOG_DEBUG(text ": sect = %llu size = %u",                              \
                  (unsigned long long)bio_sector(bio), bio_size(bio) / 512)

extern int dattobd_debug;

#endif
