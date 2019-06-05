/*
 * Copyright (c) 2017-2019, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "region_defs.h"
#include "tfm_core.h"
#include "tfm_internal.h"
#include "tfm_api.h"
#include "platform/include/tfm_spm_hal.h"
#include "uart_stdout.h"
#include "secure_utilities.h"
#include "secure_fw/spm/spm_api.h"
#include "secure_fw/include/tfm_spm_services_api.h"
#ifdef TFM_PSA_API
#include "psa_client.h"
#include "psa_service.h"
#include "tfm_thread.h"
#include "tfm_wait.h"
#include "tfm_message_queue.h"
#include "tfm_spm.h"
#include "tfm_spe_mailbox.h"
#endif

/*
 * Avoids the semihosting issue
 * FixMe: describe 'semihosting issue'
 */
#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
__asm("  .global __ARM_use_no_argv\n");
#endif

#if defined ( __GNUC__ )
/* The macro cmse_nsfptr_create defined in the gcc library uses the non-standard
 * gcc C lanuage extension 'typeof'. TF-M is built with '-std=c99' so typeof
 * cannot be used in the code. As a workaround cmse_nsfptr_create is redefined
 * here to use only standard language elements. */
#undef cmse_nsfptr_create
#define cmse_nsfptr_create(p) ((intptr_t) (p) & ~1)
#endif

#ifndef TFM_LVL
#error TFM_LVL is not defined!
#endif
#if (TFM_LVL != 1) && (TFM_LVL != 3)
#error Only TFM_LVL 1 and 3 are supported!
#endif

/* Macros to pick linker symbols and allow to form the partition data base */
#define REGION(a, b, c) a##b##c
#define REGION_NAME(a, b, c) REGION(a, b, c)
#define REGION_DECLARE(a, b, c) extern uint32_t REGION_NAME(a, b, c)

REGION_DECLARE(Image$$, TFM_UNPRIV_SCRATCH, $$ZI$$Base);
REGION_DECLARE(Image$$, TFM_UNPRIV_SCRATCH, $$ZI$$Limit);

#if 0
void configure_ns_code(void)
{
    /* SCB_NS.VTOR points to the Non-secure vector table base address */
    SCB_NS->VTOR = tfm_spm_hal_get_ns_VTOR();

    /* Setups Main stack pointer of the non-secure code */
    uint32_t ns_msp = tfm_spm_hal_get_ns_MSP();

    __TZ_set_MSP_NS(ns_msp);

    /* Get the address of non-secure code entry point to jump there */
//    uint32_t entry_ptr = tfm_spm_hal_get_ns_entry_point();

    /* Clears LSB of the function address to indicate the function-call
     * will perform the switch from secure to non-secure
     */
    //ns_entry = (nsfptr_t) cmse_nsfptr_create(entry_ptr);
}
#endif

int32_t tfm_core_init(void)
{
    /* Enables fault handlers */
    enable_fault_handlers();

    /* Configures the system reset request properties */
    system_reset_cfg();

    /* Configures debug authentication */
//    tfm_spm_hal_init_debug();

    __enable_irq();

    stdio_init();
    LOG_MSG("Secure image initializing!");

#ifdef TFM_CORE_DEBUG
    printf("TFM level is: %d\r\n", TFM_LVL);
#endif

    tfm_core_validate_boot_data();

    tfm_spm_hal_init_isolation_hw();

    //configure_ns_code();

    /* Configures all interrupts to retarget NS state, except for
     * secure peripherals
     */
    nvic_interrupt_target_state_cfg();
    /* Enable secure peripherals interrupts */
    nvic_interrupt_enable();

    tfm_scratch_area =
        (uint8_t *)&REGION_NAME(Image$$, TFM_UNPRIV_SCRATCH, $$ZI$$Base);
    tfm_scratch_area_size =
        (uint32_t)&REGION_NAME(Image$$, TFM_UNPRIV_SCRATCH, $$ZI$$Limit) -
        (uint32_t)&REGION_NAME(Image$$, TFM_UNPRIV_SCRATCH, $$ZI$$Base);
    return 0;
}

#if !TFM_MULTI_CORE_TOPOLOGY
static int32_t tfm_core_set_secure_exception_priorities(void)
{
    uint32_t VECTKEY;
    SCB_Type *scb = SCB;
    uint32_t AIRCR;

    /* Set PRIS flag is AIRCR */
    AIRCR = scb->AIRCR;
    VECTKEY = (~AIRCR & SCB_AIRCR_VECTKEYSTAT_Msk);
#if 0
    scb->AIRCR = SCB_AIRCR_PRIS_Msk |
                 VECTKEY |
                 (AIRCR & ~SCB_AIRCR_VECTKEY_Msk);
#endif

    /* FixMe: Explicitly set secure fault and Secure SVC priority to highest */

    return TFM_SUCCESS;
}
#endif

void tfm_core_spm_request_handler(const struct tfm_exc_stack_t *svc_ctx)
{
    uint32_t *res_ptr = (uint32_t *)&svc_ctx->R0;

    /* FixMe: check if caller partition is permitted to make an SPM request */

    switch (svc_ctx->R0) {
    case TFM_SPM_REQUEST_RESET_VOTE:
        /* FixMe: this is a placeholder for checks to be performed before
         * allowing execution of reset
         */
        *res_ptr = TFM_SUCCESS;
        break;
    default:
        *res_ptr = TFM_ERROR_INVALID_PARAMETER;
    }
}

int main(void)
{
    tfm_core_init();
    tfm_spm_db_init();

    tfm_spm_hal_setup_isolation_hw();

    tfm_spm_partition_set_state(TFM_SP_CORE_ID, SPM_PARTITION_STATE_RUNNING);

    extern uint32_t Image$$ARM_LIB_STACK$$ZI$$Base[];
//    uint32_t psp_stack_bottom = (uint32_t)Image$$ARM_LIB_STACK$$ZI$$Base;

//    __set_PSPLIM(psp_stack_bottom);

//    if (tfm_spm_partition_init() != SPM_ERR_OK) {
//        /* Certain systems might refuse to boot altogether if partitions fail
//         * to initialize. This is a placeholder for such an error handler
//         */
//    }
//
#ifdef TFM_PSA_API
    tfm_spm_init();
#endif

#if TFM_MULTI_CORE_TOPOLOGY

    tfm_spm_hal_boot_ns_cpu(tfm_spm_hal_get_ns_VTOR());
    tfm_spm_hal_wait_for_ns_cpu_ready();

    tfm_mailbox_init();

    /*
     * FIXME
     * In latest TF-M implementatoin on master branch, the main() will be
     * switched out finally.
     * This dead loop can be removed after applying the changes of latest
     * TF-M implementation on master branch.
     */
    while (1) {};
#else

#ifdef TFM_CORE_DEBUG
    /* Jumps to non-secure code */
    LOG_MSG("Jumping to non-secure code...");
#endif

    /* We close the TFM_SP_CORE_ID partition, because its only purpose is
     * to be able to pass the state checks for the tests started from secure.
     */
    tfm_spm_partition_set_state(TFM_SP_CORE_ID, SPM_PARTITION_STATE_CLOSED);
    tfm_spm_partition_set_state(TFM_SP_NON_SECURE_ID,
                              SPM_PARTITION_STATE_RUNNING);

    /* Prioritise secure exceptions to avoid NS being able to pre-empt secure
     * SVC or SecureFault
     */
    tfm_core_set_secure_exception_priorities();

    jump_to_ns_code();
#endif
}
