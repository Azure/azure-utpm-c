// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include "azure_c_shared_utility/umock_c_prod.h"
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/buffer_.h"

#include "azure_hub_modules/tpm_comm.h"
#include <Tbs.h>

typedef struct TPM_COMM_INFO_TAG
{
    uint32_t timeout_value;
    TBS_HCONTEXT tbs_context;
} TPM_COMM_INFO;

TPM_COMM_HANDLE tpm_comm_create()
{
    TPM_COMM_INFO* result;
    if ((result = malloc(sizeof(TPM_COMM_INFO))) == NULL)
    {
        LogError("Failure: malloc tpm communication info.");
    }
    else
    {
        TBS_RESULT tbs_res;
        TBS_CONTEXT_PARAMS2 parms = { TBS_CONTEXT_VERSION_TWO };
        TPM_DEVICE_INFO device_info = { 1, 0 };

        parms.includeTpm20 = TRUE;

        memset(result, 0, sizeof(TPM_COMM_INFO));
        tbs_res = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&parms, &result->tbs_context);
        if (tbs_res != TBS_SUCCESS)
        {
            LogError("Failure: Tbsi_Context_Create %u.", tbs_res);
            free(result);
            result = NULL;
        }
        else
        {
            tbs_res = Tbsi_GetDeviceInfo(sizeof(device_info), &device_info);
            if (tbs_res != TBS_SUCCESS)
            {
                LogError("Failure getting device tpm information %u.", tbs_res);
                Tbsip_Context_Close(&result->tbs_context);
                free(result);
                result = NULL;
            }
            else if (device_info.tpmVersion != TPM_VERSION_20)
            {
                LogError("Failure Invalid tpm version specified.  Requires 2.0.");
                Tbsip_Context_Close(&result->tbs_context);
                free(result);
                result = NULL;
            }
        }
    }
    return result;
}

void tpm_comm_destroy(TPM_COMM_HANDLE handle)
{
    if (handle)
    {
        Tbsip_Context_Close(&handle->tbs_context);
        free(handle);
    }
}

TPM_COMM_TYPE tpm_comm_get_type(TPM_COMM_HANDLE handle)
{
    (void)handle;
    return TPM_COMM_TYPE_WINDOW;
}

int tpm_comm_submit_command(TPM_COMM_HANDLE handle, const unsigned char* cmd_bytes, uint32_t bytes_len, unsigned char* response, uint32_t* resp_len)
{
    int result;
    if (handle == NULL || cmd_bytes == NULL || response == NULL || resp_len == NULL)
    {
        LogError("Invalid argument specified handle: %p, cmd_bytes: %p, response: %p, resp_len: %p.", handle, cmd_bytes, response, resp_len);
        result = __FAILURE__;
    }
    else
    {
        TBS_RESULT tbs_res;
        tbs_res = Tbsip_Submit_Command(handle->tbs_context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
            cmd_bytes, bytes_len, response, resp_len);
        if (tbs_res != TBS_SUCCESS)
        {
            LogError("Failure sending command to tpm %d.", tbs_res);
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}
