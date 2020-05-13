/*
 * Copyright (c) 2019-2021, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <string.h>

#include "cmsis_compiler.h"
#include "tfm_ns_mailbox.h"

/* The pointer to NSPE mailbox queue */
static struct ns_mailbox_queue_t *mailbox_queue_ptr = NULL;

static int32_t mailbox_wait_reply(uint8_t idx);

static inline void clear_queue_slot_empty(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->empty_slots &= ~(1 << idx);
    }
}

static inline void set_queue_slot_empty(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->empty_slots |= (1 << idx);
    }
}

static inline void set_queue_slot_pend(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->pend_slots |= (1 << idx);
    }
}

static inline void clear_queue_slot_replied(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->replied_slots &= ~(1 << idx);
    }
}

static inline void clear_queue_slot_all_replied(mailbox_queue_status_t status)
{
    mailbox_queue_ptr->replied_slots &= ~status;
}

static inline bool is_queue_slot_replied(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        return mailbox_queue_ptr->replied_slots & (1UL << idx);
    }

    return false;
}

static inline void set_queue_slot_woken(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->queue[idx].is_woken = true;
    }
}

static inline bool is_queue_slot_woken(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        return mailbox_queue_ptr->queue[idx].is_woken;
    }

    return false;
}

static inline void clear_queue_slot_woken(uint8_t idx)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->queue[idx].is_woken = false;
    }
}

static uint8_t acquire_empty_slot(struct ns_mailbox_queue_t *queue)
{
    uint8_t idx;
    mailbox_queue_status_t status;

    tfm_ns_mailbox_hal_enter_critical();
    status = queue->empty_slots;

    if (!status) {
        /* No empty slot */
        tfm_ns_mailbox_hal_exit_critical();
        return NUM_MAILBOX_QUEUE_SLOT;
    }

    for (idx = 0; idx < NUM_MAILBOX_QUEUE_SLOT; idx++) {
        if (status & (1 << idx)) {
            break;
        }
    }

    clear_queue_slot_empty(idx);

    tfm_ns_mailbox_hal_exit_critical();

    return idx;
}

static void set_msg_owner(uint8_t idx, const void *owner)
{
    if (idx < NUM_MAILBOX_QUEUE_SLOT) {
        mailbox_queue_ptr->queue[idx].owner = owner;
    }
}

#ifdef TFM_MULTI_CORE_TEST
/*
 * When NSPE mailbox only covers a single non-secure core, spinlock is only
 * required to disable IRQ.
 */
static inline void ns_mailbox_spin_lock(void)
{
    __disable_irq();
}

static inline void ns_mailbox_spin_unlock(void)
{
    __enable_irq();
}

void tfm_ns_mailbox_tx_stats_init(void)
{
    if (!mailbox_queue_ptr) {
        return;
    }

    mailbox_queue_ptr->nr_tx = 0;
    mailbox_queue_ptr->nr_used_slots = 0;
}

static void mailbox_tx_stats_update(struct ns_mailbox_queue_t *ns_queue)
{
    mailbox_queue_status_t empty_status;
    uint8_t idx, nr_empty = 0;

    if (!ns_queue) {
        return;
    }

    tfm_ns_mailbox_hal_enter_critical();
    /* Count the number of used slots when this tx arrives */
    empty_status = ns_queue->empty_slots;
    tfm_ns_mailbox_hal_exit_critical();

    if (empty_status) {
        for (idx = 0; idx < NUM_MAILBOX_QUEUE_SLOT; idx++) {
            if (empty_status & (0x1UL << idx)) {
                nr_empty++;
            }
        }
    }

    ns_mailbox_spin_lock();
    ns_queue->nr_used_slots += (NUM_MAILBOX_QUEUE_SLOT - nr_empty);
    ns_queue->nr_tx++;
    ns_mailbox_spin_unlock();
}

void tfm_ns_mailbox_stats_avg_slot(struct ns_mailbox_stats_res_t *stats_res)
{
    uint32_t nr_used_slots, nr_tx;

    if (!mailbox_queue_ptr || !stats_res) {
        return;
    }

    nr_used_slots = mailbox_queue_ptr->nr_used_slots;
    nr_tx = mailbox_queue_ptr->nr_tx;

    stats_res->avg_nr_slots = nr_used_slots / nr_tx;
    nr_used_slots %= nr_tx;
    stats_res->avg_nr_slots_tenths = nr_used_slots * 10 / nr_tx;
}
#endif

static int32_t mailbox_tx_client_req(uint32_t call_type,
                                     const struct psa_client_params_t *params,
                                     int32_t client_id,
                                     uint8_t *slot_idx)
{
    uint8_t idx;
    struct mailbox_msg_t *msg_ptr;
    const void *task_handle;

    idx = acquire_empty_slot(mailbox_queue_ptr);
    if (idx >= NUM_MAILBOX_QUEUE_SLOT) {
        return MAILBOX_QUEUE_FULL;
    }

#ifdef TFM_MULTI_CORE_TEST
    mailbox_tx_stats_update(mailbox_queue_ptr);
#endif

    /* Fill the mailbox message */
    msg_ptr = &mailbox_queue_ptr->queue[idx].msg;

    msg_ptr->call_type = call_type;
    memcpy(&msg_ptr->params, params, sizeof(msg_ptr->params));
    msg_ptr->client_id = client_id;

    /*
     * Fetch the current task handle. The task will be woken up according the
     * handle value set in the owner field.
     */
    task_handle = tfm_ns_mailbox_os_get_task_handle();
    set_msg_owner(idx, task_handle);

    tfm_ns_mailbox_hal_enter_critical();
    set_queue_slot_pend(idx);
    tfm_ns_mailbox_hal_exit_critical();

    tfm_ns_mailbox_hal_notify_peer();

    *slot_idx = idx;

    return MAILBOX_SUCCESS;
}

static int32_t mailbox_rx_client_reply(uint8_t idx, int32_t *reply)
{
    *reply = mailbox_queue_ptr->queue[idx].reply.return_val;

    /* Clear up the owner field */
    set_msg_owner(idx, NULL);

    tfm_ns_mailbox_hal_enter_critical();
    clear_queue_slot_woken(idx);
    /*
     * Make sure that the empty flag is set after all the other status flags are
     * re-initialized.
     */
    set_queue_slot_empty(idx);
    tfm_ns_mailbox_hal_exit_critical();

    return MAILBOX_SUCCESS;
}

int32_t tfm_ns_mailbox_client_call(uint32_t call_type,
                                   const struct psa_client_params_t *params,
                                   int32_t client_id,
                                   int32_t *reply)
{
    uint8_t slot_idx = NUM_MAILBOX_QUEUE_SLOT;
    int32_t reply_buf = 0x0;
    int32_t ret;

    if (!mailbox_queue_ptr) {
        return MAILBOX_INIT_ERROR;
    }

    if (!params || !reply) {
        return MAILBOX_INVAL_PARAMS;
    }

    if (tfm_ns_mailbox_os_lock_acquire() != MAILBOX_SUCCESS) {
        return MAILBOX_QUEUE_FULL;
    }

    /* It requires SVCall if NS mailbox is put in privileged mode. */
    ret = mailbox_tx_client_req(call_type, params, client_id, &slot_idx);
    if (ret != MAILBOX_SUCCESS) {
        goto exit;
    }

    mailbox_wait_reply(slot_idx);

    /* It requires SVCall if NS mailbox is put in privileged mode. */
    ret = mailbox_rx_client_reply(slot_idx, &reply_buf);
    if (ret == MAILBOX_SUCCESS) {
        *reply = reply_buf;
    }

exit:
    if (tfm_ns_mailbox_os_lock_release() != MAILBOX_SUCCESS) {
        return MAILBOX_GENERIC_ERROR;
    }

    return ret;
}

#ifdef TFM_MULTI_CORE_MULTI_CLIENT_CALL
int32_t tfm_ns_mailbox_wake_reply_owner_isr(void)
{
    uint8_t idx;
    mailbox_queue_status_t replied_status;

    if (!mailbox_queue_ptr) {
        return MAILBOX_INIT_ERROR;
    }

    tfm_ns_mailbox_hal_enter_critical_isr();
    replied_status = mailbox_queue_ptr->replied_slots;
    clear_queue_slot_all_replied(replied_status);
    tfm_ns_mailbox_hal_exit_critical_isr();

    if (!replied_status) {
        return MAILBOX_NO_PEND_EVENT;
    }

    for (idx = 0; idx < NUM_MAILBOX_QUEUE_SLOT; idx++) {
        /*
         * The reply has already received from SPE mailbox but
         * the wake-up signal is not sent yet.
         */
        if (!(replied_status & (0x1UL << idx))) {
            continue;
        }

        /* Set woken-up flag */
        tfm_ns_mailbox_hal_enter_critical_isr();
        set_queue_slot_woken(idx);
        tfm_ns_mailbox_hal_exit_critical_isr();

        tfm_ns_mailbox_os_wake_task_isr(mailbox_queue_ptr->queue[idx].owner);

        replied_status &= ~(0x1UL << idx);
        if (!replied_status) {
            break;
        }
    }

    return MAILBOX_SUCCESS;
}
#endif

int32_t tfm_ns_mailbox_init(struct ns_mailbox_queue_t *queue)
{
    int32_t ret;

    if (!queue) {
        return MAILBOX_INVAL_PARAMS;
    }

    /*
     * Further verification of mailbox queue address may be required according
     * to non-secure memory assignment.
     */

    memset(queue, 0, sizeof(*queue));

    /* Initialize empty bitmask */
    queue->empty_slots =
            (mailbox_queue_status_t)((1UL << (NUM_MAILBOX_QUEUE_SLOT - 1)) - 1);
    queue->empty_slots +=
            (mailbox_queue_status_t)(1UL << (NUM_MAILBOX_QUEUE_SLOT - 1));

    mailbox_queue_ptr = queue;

    /* Platform specific initialization. */
    ret = tfm_ns_mailbox_hal_init(queue);
    if (ret != MAILBOX_SUCCESS) {
        return ret;
    }

    ret = tfm_ns_mailbox_os_lock_init();

#ifdef TFM_MULTI_CORE_TEST
    tfm_ns_mailbox_tx_stats_init();
#endif

    return ret;
}

static int32_t mailbox_wait_reply(uint8_t idx)
{
    while (1) {
        tfm_ns_mailbox_os_wait_reply();

        /*
         * Woken up from sleep
         * Check the completed flag to make sure that the current thread is
         * woken up by reply event, rather than other events.
         */
        tfm_ns_mailbox_hal_enter_critical();
        /*
         * It requires SVCall to access NS mailbox flags if NS mailbox is put
         * in privileged mode.
         * An alternative is to let NS thread allocate its own is_woken flag.
         * But a spinlock-like mechanism is still required.
         */
#ifdef TFM_MULTI_CORE_MULTI_CLIENT_CALL
        if (is_queue_slot_woken(idx)) {
            clear_queue_slot_woken(idx);
            break;
        }
#else
        if (is_queue_slot_replied(idx)) {
            clear_queue_slot_replied(idx);
            break;
        }
#endif
        tfm_ns_mailbox_hal_exit_critical();
    }

    tfm_ns_mailbox_hal_exit_critical();

    return MAILBOX_SUCCESS;
}
