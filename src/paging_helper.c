// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"
#include "paging_helper.h"

//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#ifndef X86_CR0_WP
#define X86_CR0_WP (1UL << 16)
#endif

void disable_page_protection(unsigned long *cr0)
{
        *cr0 = read_cr0();
        write_cr0(*cr0 & ~X86_CR0_WP);
}

void reenable_page_protection(unsigned long *cr0)
{
        write_cr0(*cr0);
}
