// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2025 Datto Inc.
 */

#include "memory.h"

unsigned long dattobd_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags){
#if __GET_UNMAPPED_AREA_ADDR
    unsigned long (*__get_unmapped_area)(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags, vm_flags_t vm_flags) = (__GET_UNMAPPED_AREA_ADDR != 0) ?
    (unsigned long (*)(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags, vm_flags_t vm_flags)) (__GET_UNMAPPED_AREA_ADDR + (long long)(((void *)kfree) - (void *)KFREE_ADDR)) : NULL;

    return __get_unmapped_area(file, addr, len, pgoff, flags, 0);
#else
    return get_unmapped_area(file, addr, len, pgoff, flags);
#endif
}