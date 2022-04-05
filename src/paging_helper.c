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

/**
 * wp_cr0 - Sets the cr0 cpu register to given value.
 *
 * Enabling/disabling approach with usage of the write_cr0 function stopped
 * working sometime around Linux kernels 5.X (maybe 5.3) after this patch:
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=8dbec27a242cd3e2816eeb98d3237b9f57cf6232
 * ... hence this wrapper which does a straight up mov for >= 5.9 ; for older
 * kernels we keep write_cr0().
 *
 */
static inline void wp_cr0(unsigned long cr0) {
#ifdef USE_BDOPS_SUBMIT_BIO
        __asm__ __volatile__ ("mov %0, %%cr0": "+r" (cr0));
#else
        write_cr0(cr0);
#endif
}

/**
 * disable_page_protection() - Clears the WP flag in the CR0 register.  This
 * is a necessary step to pseudo-disabling page-level protection.
 *
 * @cr0: Output resulting from reading the CR0 register before making any
 *       modifications.
 */
void disable_page_protection(unsigned long* cr0) {
        *cr0 = read_cr0();
        wp_cr0(*cr0 & ~X86_CR0_WP);
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
