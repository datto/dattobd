// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "paging_helper.h"
#include "includes.h"

//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#ifndef X86_CR0_WP
#define X86_CR0_WP (1UL << 16)
#endif

/**
 * disable_page_protection() - Clears the WP flag in the CR0 register.  This
 * is a necessary step to pseudo-disabling page-level protection.
 *
 * @cr0: Output resulting from reading the CR0 register before making any
 *       modifications.
 */
void disable_page_protection(unsigned long *cr0)
{
        *cr0 = read_cr0();
        write_cr0(*cr0 & ~X86_CR0_WP);
}

/**
 * reenable_page_protection() - Writes the value of @cr0 to the CR0 register.
 * @cr0: the value to be written to the CR0 register.
 *
 * The value of @cr0 should be carried over from a call to
 * disable_page_protection().
 */
void reenable_page_protection(unsigned long *cr0)
{
        write_cr0(*cr0);
}
