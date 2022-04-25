// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "sset_list.h"

/**
 * sset_list_init() - Initializes the @sl structure.
 *
 * @sl: The &struct sset_list object pointer.
 */
inline void sset_list_init(struct sset_list *sl)
{
        sl->head = sl->tail = NULL;
}

/**
 * sset_list_empty() - Checks to see if the the supplied &struct sset_list
 * is empty.
 *
 * @sl: The &struct sset_list object pointer.
 *
 * Return:
 * * 0 when empty
 * * !0 otherwise
 */
inline int sset_list_empty(const struct sset_list *sl)
{
        return sl->head == NULL;
}

/**
 * sset_list_add() - Adds @sset to the tail of the list @sl.
 *
 * @sl: The &struct sset_list object pointer.
 * @sset: The &struct sector_set object pointer to be added to the @sl.
 */
void sset_list_add(struct sset_list *sl, struct sector_set *sset)
{
        sset->next = NULL;
        if (sl->tail)
                sl->tail->next = sset;
        else
                sl->head = sset;
        sl->tail = sset;
}

/**
 * sset_list_pop() - Fetches an element from the head of @sl.
 *
 * @sl: The &struct sset_list object pointer.
 *
 * The returned element is removed from @sl if present.
 *
 * Return: The element at the head of the list, NULL if empty.
 */
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
