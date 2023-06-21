// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef SYSTEM_CALL_HOOKING_H_
#define SYSTEM_CALL_HOOKING_H_

/***************************SYSTEM CALL HOOKING***************************/

void restore_system_call_table(void);

int hook_system_call_table(void);

#endif /* SYSTEM_CALL_HOOKING_H_ */
