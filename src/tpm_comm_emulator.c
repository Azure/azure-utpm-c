// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "azure_c_shared_utility/umock_c_prod.h"
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/socketio.h"
#include "azure_c_shared_utility/xio.h"
#include "azure_c_shared_utility/tickcounter.h"

#include "azure_utpm_c/tpm_comm.h"

#ifdef WIN32
#include <Winsock2.h>
#else
#include <arpa/inet.h>
#endif

#define TPM_SIMULATOR_PORT              2321
#define TPM_SIMULATOR_PLATFORM_PORT     2322
#define DEFAULT_SOCKET_TIMEOUT          20

#define REMOTE_SIGNAL_POWER_ON_CMD      1
#define REMOTE_SEND_COMMAND             8
#define REMOTE_SIGNAL_NV_ON_CMD         11
#define REMOTE_HANDSHAKE_CMD            15
#define REMOTE_SESSION_END_CMD          20

static const char* TPM_SIMULATOR_ADDRESS = "127.0.0.1";

typedef struct SOCKET_CMD_INFO_TAG
{
    bool error_encountered;
    bool send_completed;
    unsigned char* recv_buff;
    size_t length;
} SOCKET_CMD_INFO;

typedef struct TPM_COMM_INFO_TAG
{
    XIO_HANDLE xio_conn;
    bool bytes_recv_complete;
    bool socket_connected;
    bool error_state;
    TICK_COUNTER_HANDLE tick_cntr;
    uint32_t timeout_value;
    unsigned char* recv_bytes;
    size_t recv_length;
} TPM_COMM_INFO;

enum TpmSimCommands
{
    Remote_SignalPowerOn = 1,
    //SignalPowerOff = 2,
    Remote_SendCommand = 8,
    Remote_SignalNvOn = 11,
    //SignalNvOff = 12,
    Remote_Handshake = 15,
    Remote_SessionEnd = 20,
    Remote_Stop = 21,
};

static int add_to_buffer(TPM_COMM_INFO* comm_info, const unsigned char* bytes, size_t length)
{
    int result;
    unsigned char* new_buff;
    if (comm_info->recv_bytes == NULL)
    {
        new_buff = (unsigned char*)malloc(length);
    }
    else
    {
        new_buff = (unsigned char*)realloc(comm_info->recv_bytes, comm_info->recv_length+length);
    }
    if (new_buff == NULL)
    {
        comm_info->error_state = true;
        result = __FAILURE__;
    }
    else
    {
        comm_info->recv_bytes = new_buff;
        memcpy(comm_info->recv_bytes+comm_info->recv_length, bytes, length);
        comm_info->recv_length += length;
        comm_info->bytes_recv_complete = true;
        result = 0;
    }
    return result;
}

static void remove_from_buffer(TPM_COMM_INFO* comm_info, size_t length)
{
    if (comm_info->recv_length == length)
    {
        free(comm_info->recv_bytes);
        comm_info->recv_bytes = NULL;
        comm_info->recv_length = 0;
    }
    else
    {
        unsigned char* new_buff = (unsigned char*)malloc(comm_info->recv_length-length);
        memcpy(new_buff, &comm_info->recv_bytes[length], comm_info->recv_length-length);
        free(comm_info->recv_bytes);
        comm_info->recv_bytes = new_buff;
        comm_info->recv_length -= length;
    }
}

static void on_open_complete(void* context, IO_OPEN_RESULT open_result)
{
    if (context != NULL)
    {
        TPM_COMM_INFO* tpm_comm_info = (TPM_COMM_INFO*)context;
        if (open_result != IO_OPEN_OK)
        {
            LogError("Failure: tpm_comm open failed.");
            tpm_comm_info->socket_connected = false;
        }
        else
        {
            tpm_comm_info->socket_connected = true;
        }
    }
}

static void on_bytes_recieved(void* context, const unsigned char* buffer, size_t size)
{
    if (context != NULL)
    {
        TPM_COMM_INFO* tpm_comm_info = (TPM_COMM_INFO*)context;

        if (add_to_buffer(tpm_comm_info, buffer, size) != 0)
        {
            LogError("Failure: adding bytes to buffer.");
        }
    }
}

static void on_send_complete(void* context, IO_SEND_RESULT send_result)
{
    if (context != NULL)
    {
        SOCKET_CMD_INFO* cmd_info = (SOCKET_CMD_INFO*)context;
        if (send_result == IO_SEND_OK)
        {
            cmd_info->send_completed = true;
        }
    }
}

static void on_error(void* context)
{
    if (context != NULL)
    {
        TPM_COMM_INFO* tpm_comm_info = (TPM_COMM_INFO*)context;
        tpm_comm_info->error_state = true;
    }
}

static void on_close_complete(void* context)
{
    if (context != NULL)
    {
        TPM_COMM_INFO* tpm_comm_info = (TPM_COMM_INFO*)context;
        tpm_comm_info->socket_connected = false;
    }
}

static int wait_to_complete(XIO_HANDLE xio, TICK_COUNTER_HANDLE tick_cntr, bool* error_state, uint32_t timeout, const bool* wait_complete)
{
    tickcounter_ms_t init_time_ms;
    tickcounter_ms_t operation_tm;

    (void)tickcounter_get_current_ms(tick_cntr, &init_time_ms);
    do
    {
        xio_dowork(xio);
        (void)tickcounter_get_current_ms(tick_cntr, &operation_tm);
    } while (!*wait_complete && !*error_state && ((operation_tm-init_time_ms)/1000) < timeout);
    return *wait_complete ? 0 : __FAILURE__;
}

static int read_sync_bytes(TPM_COMM_INFO* comm_info, unsigned char* tpm_bytes, uint32_t* bytes_len)
{
    int result;

    // Do I have enough bytes
    for (size_t index = 0; index < 2; index++)
    {
        if (comm_info->recv_length >= *bytes_len)
        {
            memcpy(tpm_bytes, comm_info->recv_bytes, *bytes_len);
            remove_from_buffer(comm_info, *bytes_len);
            result = 0;
            break;
        }
        else
        {
            result = wait_to_complete(comm_info->xio_conn, comm_info->tick_cntr, &comm_info->error_state, comm_info->timeout_value, &comm_info->bytes_recv_complete);
            if (comm_info->bytes_recv_complete)
            {
                comm_info->bytes_recv_complete = false;
            }
            else
            {
                LogError("Failure received bytes timed out.");
                result = __FAILURE__;
            }
        }
    }
    return result;
}

static int read_sync_cmd(TPM_COMM_INFO* tpm_comm_info, uint32_t* tpm_bytes)
{
    int result;
    uint32_t bytes_len = sizeof(uint32_t);
    result = read_sync_bytes(tpm_comm_info, (unsigned char*)tpm_bytes, &bytes_len);
    if (result == 0)
    {
        *tpm_bytes = htonl(*tpm_bytes);
    }
    return result;
}

static bool is_ack_ok(TPM_COMM_INFO* tpm_comm_info)
{
    uint32_t end_tag;
    return (read_sync_cmd(tpm_comm_info, &end_tag) == 0 && end_tag == 0);
}

static int send_xio_sync_bytes(XIO_HANDLE xio, TICK_COUNTER_HANDLE tick_cntr, bool* error_state, uint32_t timeout, const unsigned char* cmd_val, size_t byte_len)
{
    int result;
    SOCKET_CMD_INFO cmd_info;
    cmd_info.send_completed = false;
    cmd_info.recv_buff = NULL;
    cmd_info.length = 0;

    if (xio_send(xio, cmd_val, byte_len, on_send_complete, &cmd_info) == 0)
    {
        result = wait_to_complete(xio, tick_cntr, error_state, timeout, &cmd_info.send_completed);
    }
    else
    {
        LogError("Failure sending packet.");
        result = __FAILURE__;
    }
    return result;
}

static int send_sync_bytes(TPM_COMM_INFO* comm_info, const unsigned char* cmd_val, size_t byte_len)
{
    return send_xio_sync_bytes(comm_info->xio_conn, comm_info->tick_cntr, &comm_info->error_state, comm_info->timeout_value, cmd_val, byte_len);
}

static int send_sync_cmd(TPM_COMM_INFO* tpm_comm_info, uint32_t cmd_val)
{
    uint32_t net_bytes = htonl(cmd_val);
    return send_sync_bytes(tpm_comm_info, (const unsigned char*)&net_bytes, sizeof(uint32_t) );
}

static void close_simulator(TPM_COMM_INFO* tpm_comm_info)
{
    if (tpm_comm_info->socket_connected)
    {
        (void)send_sync_cmd(tpm_comm_info, REMOTE_SESSION_END_CMD);
        (void)xio_close(tpm_comm_info->xio_conn, on_close_complete, &tpm_comm_info);
    }
}

static void platform_bytes_received(void* context, const unsigned char* buffer, size_t size)
{
    if (context != NULL)
    {
        SOCKET_CMD_INFO* cmd_info = (SOCKET_CMD_INFO*)context;
        if (cmd_info->recv_buff != NULL)
        {
            free(cmd_info->recv_buff);
        }
        cmd_info->recv_buff = (unsigned char*)malloc(size);
        if (cmd_info->recv_buff == NULL)
        {
            LogError("Failure allocating bytes received.");
            cmd_info->error_encountered = true;
        }
        else
        {
            memcpy(cmd_info->recv_buff, buffer, size);
            cmd_info->length = size;
            cmd_info->send_completed = true;
        }
    }
}

static void platform_on_error(void* context)
{
    if (context != NULL)
    {
        SOCKET_CMD_INFO* cmd_info = (SOCKET_CMD_INFO*)context;
        cmd_info->error_encountered = true;
    }
}

static int power_on_simulator(TPM_COMM_INFO* comm_info)
{
    int result;

    SOCKETIO_CONFIG socketio_config;
    socketio_config.hostname = TPM_SIMULATOR_ADDRESS;
    socketio_config.port = TPM_SIMULATOR_PLATFORM_PORT;
    socketio_config.accepted_socket = NULL;

    SOCKET_CMD_INFO cmd_info;
    cmd_info.send_completed = false;
    cmd_info.error_encountered = false;
    cmd_info.recv_buff = NULL;
    cmd_info.length = 0;

    XIO_HANDLE plat_xio;

    if ((plat_xio = xio_create(socketio_get_interface_description(), &socketio_config)) == NULL)
    {
        LogError("Failure: connecting to tpm simulator platform interface.");
        result = __FAILURE__;
    }
    else if (xio_open(plat_xio, on_open_complete, comm_info, platform_bytes_received, &cmd_info, platform_on_error, &cmd_info) != 0)
    {
        LogError("Failure: connecting to tpm simulator.");
        xio_destroy(plat_xio);
        result = __FAILURE__;
    }
    else if (wait_to_complete(plat_xio, comm_info->tick_cntr, &cmd_info.error_encountered, comm_info->timeout_value, &comm_info->socket_connected) != 0)
    {
        LogError("Failure: connecting to tpm simulator.");
        xio_destroy(plat_xio);
        result = __FAILURE__;
    }
    else
    {
        uint32_t power_on_cmd = htonl(REMOTE_SIGNAL_POWER_ON_CMD);
        uint32_t signal_nv_cmd = htonl(REMOTE_SIGNAL_NV_ON_CMD);

        if (send_xio_sync_bytes(plat_xio, comm_info->tick_cntr, &comm_info->error_state, comm_info->timeout_value, (const unsigned char*)&power_on_cmd, sizeof(uint32_t) ) != 0)
        {
            LogError("Failure sending remote handshake.");
            result = __FAILURE__;
        }
        else 
        {
            if (wait_to_complete(plat_xio, comm_info->tick_cntr, &cmd_info.error_encountered, comm_info->timeout_value, &cmd_info.send_completed) != 0)
            {
                LogError("Failure sending remote handshake.");
                result = __FAILURE__;
            }
            else
            {
                cmd_info.send_completed = false;
                uint32_t ack_value;
                memcpy(&ack_value, cmd_info.recv_buff, sizeof(uint32_t));
                ack_value = htonl(ack_value);
                if (ack_value != 0)
                {
                    LogError("Failure reading cmd sync.");
                    result = __FAILURE__;
                }
                else
                {
                    if (send_xio_sync_bytes(plat_xio, comm_info->tick_cntr, &comm_info->error_state, comm_info->timeout_value, (const unsigned char*)&signal_nv_cmd, sizeof(uint32_t) ) != 0)
                    {
                        LogError("Failure sending remote handshake.");
                        result = __FAILURE__;
                    }
                    else
                    {
                        if (wait_to_complete(plat_xio, comm_info->tick_cntr, &cmd_info.error_encountered, comm_info->timeout_value, &cmd_info.send_completed) != 0)
                        {
                            LogError("Failure sending remote handshake.");
                            result = __FAILURE__;
                        }
                        else
                        {
                            memcpy(&ack_value, cmd_info.recv_buff, sizeof(uint32_t));
                            ack_value = htonl(ack_value);
                            if (ack_value != 0)
                            {
                                LogError("Failure reading cmd sync.");
                                result = __FAILURE__;
                            }
                            else
                            {
                                result = 0;
                            }
                        }
                    }
                }
            }
            free(cmd_info.recv_buff);
        }
        xio_close(plat_xio, NULL, NULL);
        xio_dowork(plat_xio);
        xio_destroy(plat_xio);
    }
    return result;
}

static void disconnect_from_simulator(TPM_COMM_INFO* tpm_comm_info)
{
    xio_close(tpm_comm_info->xio_conn, NULL, NULL);
    xio_destroy(tpm_comm_info->xio_conn);
}

static int execute_simulator_setup(TPM_COMM_INFO* tpm_comm_info)
{
    int result;
    uint32_t tmp_client_version = 1;
    uint32_t tmp_server_version = 1;
    uint32_t tpm_info;

    // Send the handshake request
    if (send_sync_cmd(tpm_comm_info, REMOTE_HANDSHAKE_CMD) != 0)
    {
        LogError("Failure sending remote handshake.");
        result = __FAILURE__;
    }
    // Send desired protocol version
    else if (send_sync_cmd(tpm_comm_info, tmp_client_version) != 0)
    {
        LogError("Failure sending client version.");
        result = __FAILURE__;
    }
    else if (read_sync_cmd(tpm_comm_info, &tmp_server_version) != 0)
    {
        LogError("Failure reading cmd sync.");
        result = __FAILURE__;
    }
    else if (tmp_client_version != tmp_server_version)
    {
        LogError("Failure client and server version does not match.");
        result = __FAILURE__;
    }
    else if (read_sync_cmd(tpm_comm_info, &tpm_info) != 0)
    {
        LogError("Failure reading cmd sync.");
        result = __FAILURE__;
    }
    // GetAck
    else if (!is_ack_ok(tpm_comm_info))
    {
        LogError("Failure ack byte from tpm is invalid.");
        result = __FAILURE__;
    }
    else if (power_on_simulator(tpm_comm_info) != 0)
    {
        LogError("Failure powering on simulator.");
        result = __FAILURE__;
    }
    else
    {
        result = 0;
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

        SOCKETIO_CONFIG socketio_config;
        socketio_config.hostname = TPM_SIMULATOR_ADDRESS;
        socketio_config.port = TPM_SIMULATOR_PORT;
        socketio_config.accepted_socket = NULL;

        result->timeout_value = DEFAULT_SOCKET_TIMEOUT;
        if ((result->tick_cntr = tickcounter_create()) == NULL)
        {
            LogError("Failure: creating tick counter.");
            free(result);
            result = NULL;
        }
        else if ((result->xio_conn = xio_create(socketio_get_interface_description(), &socketio_config)) == NULL)
        {
            LogError("Failure: connecting to tpm simulator.");
            tickcounter_destroy(result->tick_cntr);
            free(result);
            result = NULL;
        }
        else if (xio_open(result->xio_conn, on_open_complete, result, on_bytes_recieved, result, on_error, result) != 0)
        {
            LogError("Failure: connecting to tpm simulator.");
            tickcounter_destroy(result->tick_cntr);
            xio_destroy(result->xio_conn);
            free(result);
            result = NULL;
        }
        else
        {
            if (execute_simulator_setup(result) != 0)
            {
                LogError("Failure: connecting to tpm simulator.");
                tickcounter_destroy(result->tick_cntr);
                xio_close(result->xio_conn, NULL, NULL);
                xio_destroy(result->xio_conn);
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
        tickcounter_destroy(handle->tick_cntr);
        disconnect_from_simulator(handle);
        free(handle);
    }
}

TPM_COMM_TYPE tpm_comm_get_type(TPM_COMM_HANDLE handle)
{
    (void)handle;
    return TPM_COMM_TYPE_EMULATOR;
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
        unsigned char locality = 0;
        if (send_sync_cmd(handle, Remote_SendCommand) != 0)
        {
            LogError("Failure preparing sending Remote Command");
            result = __FAILURE__;
        }
        else if (send_sync_bytes(handle, (const unsigned char*)&locality, 1) != 0)
        {
            LogError("Failure setting locality to TPM");
            result = __FAILURE__;
        }
        else if (send_sync_cmd(handle, bytes_len) != 0)
        {
            LogError("Failure writing command bit to tpm");
            result = __FAILURE__;
        }
        else if (send_sync_bytes(handle, cmd_bytes, bytes_len))
        {
            LogError("Failure writing data to tpm");
            result = __FAILURE__;
        }
        else
        {
            uint32_t length_byte;
            if (read_sync_cmd(handle, &length_byte) != 0)
            {
                LogError("Failure reading length data from tpm");
                result = __FAILURE__;
            }
            else if (length_byte > *resp_len)
            {
                LogError("Bytes read are greater then bytes expected len_bytes:%u expected: %u", length_byte, *resp_len);
                result = __FAILURE__;
            }
            else
            {
                *resp_len = length_byte;
                if (read_sync_bytes(handle, response, &length_byte) != 0)
                {
                    LogError("Failure reading bytes");
                    result = __FAILURE__;
                }
                else
                {
                    // check the Ack
                    uint32_t ack_cmd;
                    if (read_sync_cmd(handle, &ack_cmd) != 0 || ack_cmd != 0)
                    {
                        LogError("Failure reading tpm ack");
                        result = __FAILURE__;
                    }
                    else
                    {
                        result = 0;
                    }
                }
            }
        }
    }
    return result;
}
