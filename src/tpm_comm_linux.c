// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#include "azure_c_shared_utility/umock_c_prod.h"
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/xlogging.h"

#include "azure_utpm_c/tpm_comm.h"

static const char* TPM_DEVICE_NAME = "/dev/tpm0";

#define MIN_TPM_RESPONSE_LENGTH     10

typedef struct TPM_COMM_INFO_TAG
{
    uint32_t timeout_value;
    int tpm_device;
} TPM_COMM_INFO;

static int write_data_to_tpm(TPM_COMM_INFO* tpm_info, const unsigned char* tpm_bytes, uint32_t bytes_len)
{
    int result;

    if ((result->tpm_device = open(TPM_DEVICE_NAME, O_RDWR)) < 0)
    {
        LogError("Failure: opening TPM device %d:%s.", errno, strerror(errno));
        result = __FAILURE__;
    }
    else
    {
        int resp_len = write(tpm_info->tpm_device, tpm_bytes, bytes_len);
        if (resp_len != bytes_len)
        {
            LogError("Failure writing data to tpm: %d:%s.", errno, strerror(errno));
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
        close(handle->tpm_device);
    }
    return result;
}

static int read_data_from_tpm(TPM_COMM_INFO* tpm_info, unsigned char* tpm_bytes, uint32_t* bytes_len)
{
    int result;
    if ((result->tpm_device = open(TPM_DEVICE_NAME, O_RDWR)) < 0)
    {
        LogError("Failure: opening TPM device %d:%s.", errno, strerror(errno));
        result = __FAILURE__;
    }
    else
    {
        int len_read = read(tpm_info->tpm_device, tpm_bytes, *bytes_len);
        if (len_read < MIN_TPM_RESPONSE_LENGTH)
        {
            LogError("Failure reading data from tpm: len: %d - %d:%s.", len_read, errno, strerror(errno));
            result = __FAILURE__;
        }
        else
        {
            *bytes_len = len_read;
            result = 0;
        }
        close(handle->tpm_device);
    }
    return result;
}

TPM_COMM_HANDLE tpm_comm_create()
{
    TPM_COMM_INFO* result;
    if ((result = malloc(sizeof(TPM_COMM_INFO))) == NULL)
    {
        LogError("Failure: malloc tpm communication info.");
    }
    else
    {
        memset(result, 0, sizeof(TPM_COMM_INFO));
    }
    return result;
}

void tpm_comm_destroy(TPM_COMM_HANDLE handle)
{
    if (handle)
    {
        free(handle);
    }
}

TPM_COMM_TYPE tpm_comm_get_type(TPM_COMM_HANDLE handle)
{
    (void)handle;
    return TPM_COMM_TYPE_LINUX;
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
        // Send to TPM 
        if (write_data_to_tpm(handle, (const unsigned char*)cmd_bytes, bytes_len) != 0)
        {
            LogError("Failure setting locality to TPM");
            result = __FAILURE__;
        }
        else
        {
            if (read_data_from_tpm(handle, response, resp_len) != 0)
            {
                LogError("Failure reading bytes from tpm");
                result = __FAILURE__;
            }
            else
            {
                result = 0;
            }
        }
    }
    return result;
}
