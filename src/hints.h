// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef HINTS_H_
#define HINTS_H_

// macros for compilation
#define MAYBE_UNUSED(x) (void)(x)

#ifndef ACCESS_ONCE

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#endif

#endif /* HINTS_H_ */
