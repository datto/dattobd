// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "submit_bio.h"

#include "bio_helper.h" // needed for USE_BDOPS_SUBMIT_BIO to be defined
#include "callback_refs.h"
#include "includes.h"
#include "logging.h"
#include "paging_helper.h"
#include "snap_device.h"

#ifdef USE_BDOPS_SUBMIT_BIO

/*
 * For ftrace to work, each function has a preamble that calls a "function" (asm
 * snippet) called __fentry__ which then triggers the callbacks. If we want to
 * recurse without triggering ftrace, we'll need to skip this preamble. Don't
 * worry, the stack pointer manipulation is right after the call.
 */
blk_qc_t (*dattobd_submit_bio_noacct_passthrough)(struct bio *) =
	(blk_qc_t(*)(struct bio *))((unsigned long)(submit_bio_noacct) +
        FENTRY_CALL_INSTR_BYTES);

int dattobd_submit_bio_real(
    struct snap_device* dev,
    struct bio *bio)
{
    return dattobd_submit_bio_noacct_passthrough(bio);
}

#endif
