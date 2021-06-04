/*
 * Copyright (c) 2021, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/* !! For SPM load usage, no including by components out of SPM !! */
#ifndef __SPM_LOAD_API_H__
#define __SPM_LOAD_API_H__

#include "asset_defs.h"
#include "irq_defs.h"
#include "partition_defs.h"
#include "service_defs.h"
#include "spm_ipc.h"

/* Length of extendable variables in partition load type */
#define LOAD_INFO_EXT_LENGTH                2
/* Argument "pldinf" must be a "struct partition_load_info_t *". */
#define LOAD_INFSZ_BYTES(pldinf)                                       \
    (sizeof(*(pldinf)) + LOAD_INFO_EXT_LENGTH * sizeof(uintptr_t) +    \
     (pldinf)->ndeps * sizeof(uint32_t) +                              \
     (pldinf)->nservices * sizeof(struct service_load_info_t) +        \
     (pldinf)->nassets * sizeof(struct asset_desc_t) +                 \
     (pldinf)->nirqs * sizeof(struct irq_load_info_t))

/* 'Allocate' stack based on load info */
#define LOAD_ALLOCED_STACK_ADDR(pldinf)    (*((uintptr_t *)(pldinf + 1)))

#define LOAD_INFO_DEPS(pldinf)                                         \
    ((uintptr_t)(pldinf + 1) + LOAD_INFO_EXT_LENGTH * sizeof(uintptr_t))
#define LOAD_INFO_SERVICE(pldinf)                                      \
    ((uintptr_t)LOAD_INFO_DEPS(pldinf) + (pldinf)->ndeps * sizeof(uint32_t))
#define LOAD_INFO_ASSET(pldinf)                                        \
    ((uintptr_t)LOAD_INFO_SERVICE(pldinf) +                            \
     (pldinf)->nservices * sizeof(struct service_load_info_t))
#define LOAD_INFO_IRQ(pldinf)                                          \
    ((uintptr_t)LOAD_INFO_ASSET(pldinf) +                              \
     (pldinf)->nassets * sizeof(struct asset_desc_t))

/*
 * Allocate a partition object and return if a load is successful.
 * An 'assuredly' function, return NULL for no more partitions and
 * return a valid pointer if succeed. Other errors simply panic
 * the system and never return.
 */
struct partition_t *load_a_partition_assuredly(void);

/*
 * Allocated numbers of service objects based on given partition.
 * Link services with 'list_head' if it is provided. It also needs the
 * stateless service reference table and whole table size for loading.
 * As an 'assuredly' function, errors simply panic the system and never
 * return.
 */
void load_services_assuredly(struct partition_t *p_partition,
                             struct service_t **connection_services_listhead,
                             struct service_t **stateless_service_ref_tbl,
                             size_t ref_tbl_size);

/*
 * Append IRQ signals to Partition signals.
 * Set initial IRQ enabled status according to framework version.
 * And initialize the IRQ.
 * Any error within this API causes system to panic.
 */
void load_irqs_assuredly(struct partition_t *p_partition);

#endif /* __SPM_LOAD_API_H__ */