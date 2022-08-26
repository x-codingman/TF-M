/*
 * Copyright (c) 2019-2023, Arm Limited. All rights reserved.
 * Copyright (c) 2022-2023 Cypress Semiconductor Corporation (an Infineon
 * company) or an affiliate of Cypress Semiconductor Corporation. All rights
 * reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "critical_section.h"
#include "ffm/backend.h"
#include "ffm/psa_api.h"
#include "load/service_defs.h"
#include "spm.h"

/* PSA APIs only needed by connection-based services */

psa_status_t tfm_spm_client_psa_connect(uint32_t sid, uint32_t version)
{
    struct service_t *service;
    struct conn_handle_t *conn_handle;
    int32_t client_id;
    psa_handle_t handle;
    bool ns_caller = tfm_spm_is_ns_caller();
    struct critical_section_t cs_assert = CRITICAL_SECTION_STATIC_INIT;

    /*
     * It is a PROGRAMMER ERROR if the RoT Service does not exist on the
     * platform.
     */
    service = tfm_spm_get_service_by_sid(sid);
    if (!service) {
        return PSA_ERROR_CONNECTION_REFUSED;
    }

    /* It is a PROGRAMMER ERROR if connecting to a stateless service. */
    if (SERVICE_IS_STATELESS(service->p_ldinf->flags)) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }

    /*
     * It is a PROGRAMMER ERROR if the caller is not authorized to access the
     * RoT Service.
     */
    if (tfm_spm_check_authorization(sid, service, ns_caller) != PSA_SUCCESS) {
        return PSA_ERROR_CONNECTION_REFUSED;
    }

    /*
     * It is a PROGRAMMER ERROR if the version of the RoT Service requested is
     * not supported on the platform.
     */
    if (tfm_spm_check_client_version(service, version) != PSA_SUCCESS) {
        return PSA_ERROR_CONNECTION_REFUSED;
    }

    client_id = tfm_spm_get_client_id(ns_caller);

    /*
     * Create connection handle here since it is possible to return the error
     * code to client when creation fails.
     */
    CRITICAL_SECTION_ENTER(cs_assert);
    conn_handle = tfm_spm_create_conn_handle();
    CRITICAL_SECTION_LEAVE(cs_assert);
    if (!conn_handle) {
        return PSA_ERROR_CONNECTION_BUSY;
    }

    handle = tfm_spm_to_user_handle(conn_handle);
    /* No input or output needed for connect message */
    spm_fill_message(conn_handle, service, handle, PSA_IPC_CONNECT,
                     client_id, NULL, 0, NULL, 0, NULL);

    return backend_messaging(service, conn_handle);
}

psa_status_t tfm_spm_client_psa_close(psa_handle_t handle)
{
    struct service_t *service;
    struct conn_handle_t *conn_handle;
    int32_t client_id;
    bool ns_caller = tfm_spm_is_ns_caller();

    /* It will have no effect if called with the NULL handle */
    if (handle == PSA_NULL_HANDLE) {
        return PSA_SUCCESS;
    }

    /* It is a PROGRAMMER ERROR if called with a stateless handle. */
    if (IS_STATIC_HANDLE(handle)) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }

    client_id = tfm_spm_get_client_id(ns_caller);

    /*
     * It is a PROGRAMMER ERROR if an invalid handle was provided that is not
     * the null handle.
     */
    conn_handle = spm_get_handle_by_client_handle(handle, client_id);
    if (!conn_handle) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }

    service = conn_handle->service;
    if (!service) {
        /* FixMe: Need to implement one mechanism to resolve this failure. */
        return PSA_ERROR_PROGRAMMER_ERROR;
    }

    /*
     * It is a PROGRAMMER ERROR if the connection is currently handling a
     * request.
     */
    if (conn_handle->status == TFM_HANDLE_STATUS_ACTIVE) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }

    /* No input or output needed for close message */
    spm_fill_message(conn_handle, service, handle, PSA_IPC_DISCONNECT,
                     client_id, NULL, 0, NULL, 0, NULL);

    return backend_messaging(service, conn_handle);
}

void tfm_spm_partition_psa_set_rhandle(psa_handle_t msg_handle, void *rhandle)
{
    struct conn_handle_t *handle;

    /* It is a fatal error if message handle is invalid */
    handle = spm_get_handle_by_msg_handle(msg_handle);
    if (!handle) {
        tfm_core_panic();
    }

    /* It is a PROGRAMMER ERROR if a stateless service sets rhandle. */
    if (SERVICE_IS_STATELESS(handle->service->p_ldinf->flags)) {
        tfm_core_panic();
    }

    handle->msg.rhandle = rhandle;
    handle->rhandle = rhandle;
}