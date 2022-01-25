// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "sset_list.h"

inline void sset_list_init(struct sset_list *sl)
{
        sl->head = sl->tail = NULL;
}

inline int sset_list_empty(const struct sset_list *sl)
{
        return sl->head == NULL;
}

void sset_list_add(struct sset_list *sl, struct sector_set *sset)
{
        sset->next = NULL;
        if (sl->tail)
                sl->tail->next = sset;
        else
                sl->head = sset;
        sl->tail = sset;
}

struct sector_set *sset_list_pop(struct sset_list *sl)
{
        struct sector_set *sset = sl->head;

        if (sset) {
                sl->head = sl->head->next;
                if (!sl->head)
                        sl->tail = NULL;
                sset->next = NULL;
        }

        return sset;
}
