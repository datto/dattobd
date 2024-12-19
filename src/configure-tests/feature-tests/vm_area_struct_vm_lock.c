// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct vm_area_struct *vma;
    struct vma_lock vma_lock;
    
    vma->vm_lock = &vma_lock;
}
