// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifdef __cplusplus
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#else
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#endif

#include "testrunnerswitcher.h"

#undef DECLSPEC_IMPORT
#pragma warning(disable: 4273)
#ifdef WIN32
#include <Winsock2.h>
typedef unsigned long   htonl_type;
#else
#include <arpa/inet.h>
typedef uint32_t        htonl_type;
#endif

static void* my_gballoc_malloc(size_t size)
{
    return malloc(size);
}

static void my_gballoc_free(void* ptr)
{
    free(ptr);
}

static void* my_gballoc_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

#include "umock_c.h"
#include "umocktypes_charptr.h"
#include "umocktypes_stdint.h"
#include "umock_c_negative_tests.h"
#include "azure_c_shared_utility/macro_utils.h"

#define ENABLE_MOCKS
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/umock_c_prod.h"
#include "azure_c_shared_utility/socketio.h"
#include "azure_c_shared_utility/xio.h"
#include "azure_c_shared_utility/tickcounter.h"
#undef ENABLE_MOCKS

#include "azure_utpm_c/tpm_comm.h"

static htonl_type g_htonl_value = 1;

#ifdef WIN32
MOCK_FUNCTION_WITH_CODE(WSAAPI, htonl_type, htonl, htonl_type, hostlong)
#else
MOCK_FUNCTION_WITH_CODE(, htonl_type, htonl, htonl_type, hostlong)
#endif
htonl_type tmp_rtn = g_htonl_value;
if (hostlong == 0x11111111)
{
    g_htonl_value = 0;
}
MOCK_FUNCTION_END(tmp_rtn)

#ifdef __cplusplus
extern "C"
{
#endif
#ifdef __cplusplus
}
#endif

static const unsigned char* TEMP_TPM_COMMAND = (const unsigned char*)0x00012345;
#define TEMP_CMD_LENGTH         128
static const unsigned char RECV_DATA[] = { 0x11, 0x11, 0x11, 0x11 };
#define RECV_DATA_LEN           4

static ON_SEND_COMPLETE g_on_send_complete = NULL;
static void* g_on_send_context = NULL;
static ON_BYTES_RECEIVED g_on_bytes_received = NULL;
static void* g_on_bytes_received_context = NULL;

static bool g_send_was_last_called;
static bool g_closing_xio = false;

DEFINE_ENUM_STRINGS(UMOCK_C_ERROR_CODE, UMOCK_C_ERROR_CODE_VALUES)

TEST_DEFINE_ENUM_TYPE(TPM_COMM_TYPE, TPM_COMM_TYPE_VALUES);
IMPLEMENT_UMOCK_C_ENUM_TYPE(TPM_COMM_TYPE, TPM_COMM_TYPE_VALUES);

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code)
{
    char temp_str[256];
    (void)snprintf(temp_str, sizeof(temp_str), "umock_c reported error :%s", ENUM_TO_STRING(UMOCK_C_ERROR_CODE, error_code));
    ASSERT_FAIL(temp_str);
}

static TICK_COUNTER_HANDLE my_tickcounter_create(void)
{
    return (TICK_COUNTER_HANDLE)my_gballoc_malloc(1);
}

static void my_tickcounter_destroy(TICK_COUNTER_HANDLE tick_counter)
{
    my_gballoc_free(tick_counter);
}

static XIO_HANDLE my_xio_create(const IO_INTERFACE_DESCRIPTION* io_interface_description, const void* xio_create_parameters)
{
    (void)io_interface_description;
    (void)xio_create_parameters;
    return (XIO_HANDLE)my_gballoc_malloc(1);
}

static int my_xio_open(XIO_HANDLE xio,
    ON_IO_OPEN_COMPLETE on_io_open_complete, void* on_io_open_complete_context,
    ON_BYTES_RECEIVED on_bytes_received, void* on_bytes_received_context,
    ON_IO_ERROR on_io_error, void* on_io_error_context)
{
    (void)xio;
    (void)on_io_error; (void)on_io_error_context;
    g_on_bytes_received = on_bytes_received;
    g_on_bytes_received_context = on_bytes_received_context;

    on_io_open_complete(on_io_open_complete_context, IO_OPEN_OK);
    return 0;
}

static int my_xio_send(XIO_HANDLE xio, const void* buffer, size_t size, ON_SEND_COMPLETE on_send_complete, void* callback_context)
{
    (void)xio; (void)buffer;
    (void)size;

    g_send_was_last_called = true;
    g_on_send_complete = on_send_complete;
    g_on_send_context = callback_context;
    return 0;
}

static void my_xio_dowork(XIO_HANDLE xio)
{
    (void)xio;
    if (!g_closing_xio)
    {
        if (g_send_was_last_called)
        {
            g_on_send_complete(g_on_send_context, IO_SEND_OK);
            g_send_was_last_called = false;
        }
        else
        {
            g_on_bytes_received(g_on_bytes_received_context, RECV_DATA, RECV_DATA_LEN);
        }
    }
}

int my_xio_close(XIO_HANDLE xio, ON_IO_CLOSE_COMPLETE on_io_close_complete, void* on_io_close_complete_context)
{
    (void)xio;
    (void)on_io_close_complete;
    (void)on_io_close_complete_context;
    g_closing_xio = true;
    return 0;
}

static void my_xio_destroy(XIO_HANDLE xio)
{
    my_gballoc_free(xio);
}

static TEST_MUTEX_HANDLE g_testByTest;
static TEST_MUTEX_HANDLE g_dllByDll;

BEGIN_TEST_SUITE(tpm_comm_emulator_ut)

    TEST_SUITE_INITIALIZE(suite_init)
    {
        int result;

        TEST_INITIALIZE_MEMORY_DEBUG(g_dllByDll);
        g_testByTest = TEST_MUTEX_CREATE();
        ASSERT_IS_NOT_NULL(g_testByTest);

        (void)umock_c_init(on_umock_c_error);

        result = umocktypes_charptr_register_types();
        ASSERT_ARE_EQUAL(int, 0, result);
        result = umocktypes_stdint_register_types();
        ASSERT_ARE_EQUAL(int, 0, result);

        REGISTER_UMOCK_ALIAS_TYPE(HTTP_CLIENT_RESULT, int);
        REGISTER_UMOCK_ALIAS_TYPE(TPM_COMM_HANDLE, void*);
        REGISTER_UMOCK_ALIAS_TYPE(XIO_HANDLE, void*);
        REGISTER_UMOCK_ALIAS_TYPE(ON_IO_OPEN_COMPLETE, void*);
        REGISTER_UMOCK_ALIAS_TYPE(ON_BYTES_RECEIVED, void*);
        REGISTER_UMOCK_ALIAS_TYPE(ON_IO_ERROR, void*);
        REGISTER_UMOCK_ALIAS_TYPE(TICK_COUNTER_HANDLE, void*);
        REGISTER_UMOCK_ALIAS_TYPE(ON_SEND_COMPLETE, void*);
        REGISTER_UMOCK_ALIAS_TYPE(ON_IO_CLOSE_COMPLETE, void*);
        //REGISTER_UMOCK_ALIAS_TYPE(ON_IO_CLOSE_COMPLETE, void*);

        REGISTER_GLOBAL_MOCK_HOOK(gballoc_malloc, my_gballoc_malloc);
        REGISTER_GLOBAL_MOCK_FAIL_RETURN(gballoc_malloc, NULL);
        REGISTER_GLOBAL_MOCK_HOOK(gballoc_free, my_gballoc_free);
        REGISTER_GLOBAL_MOCK_HOOK(gballoc_realloc, my_gballoc_realloc);
        REGISTER_GLOBAL_MOCK_FAIL_RETURN(gballoc_realloc, NULL);

        REGISTER_GLOBAL_MOCK_HOOK(tickcounter_create, my_tickcounter_create);
        REGISTER_GLOBAL_MOCK_FAIL_RETURN(tickcounter_create, NULL);
        REGISTER_GLOBAL_MOCK_HOOK(tickcounter_destroy, my_tickcounter_destroy);

        REGISTER_GLOBAL_MOCK_HOOK(xio_create, my_xio_create);
        REGISTER_GLOBAL_MOCK_FAIL_RETURN(xio_create, NULL);
        REGISTER_GLOBAL_MOCK_HOOK(xio_destroy, my_xio_destroy);
        REGISTER_GLOBAL_MOCK_HOOK(xio_open, my_xio_open);
        REGISTER_GLOBAL_MOCK_HOOK(xio_close, my_xio_close);
        REGISTER_GLOBAL_MOCK_HOOK(xio_send, my_xio_send);
        REGISTER_GLOBAL_MOCK_HOOK(xio_dowork, my_xio_dowork);
    }

    TEST_SUITE_CLEANUP(suite_cleanup)
    {
        umock_c_deinit();

        TEST_MUTEX_DESTROY(g_testByTest);
        TEST_DEINITIALIZE_MEMORY_DEBUG(g_dllByDll);
    }

    TEST_FUNCTION_INITIALIZE(method_init)
    {
        if (TEST_MUTEX_ACQUIRE(g_testByTest))
        {
            ASSERT_FAIL("Could not acquire test serialization mutex.");
        }
        umock_c_reset_all_calls();
        g_htonl_value = 1;
        g_on_send_complete = NULL;
        g_on_send_context = NULL;
        g_on_bytes_received = NULL;
        g_on_bytes_received_context = NULL;
        g_send_was_last_called = false;
        g_closing_xio = false;
    }

    TEST_FUNCTION_CLEANUP(method_cleanup)
    {
        TEST_MUTEX_RELEASE(g_testByTest);
    }

    static int should_skip_index(size_t current_index, const size_t skip_array[], size_t length)
    {
        int result = 0;
        for (size_t index = 0; index < length; index++)
        {
            if (current_index == skip_array[index])
            {
                result = __LINE__;
                break;
            }
        }
        return result;
    }

    static void setup_wait_to_complete_mocks(bool call_on_recv)
    {
        tickcounter_ms_t init_tm = 1000;
        tickcounter_ms_t current_tm = 1010;
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&init_tm, sizeof(tickcounter_ms_t));
        STRICT_EXPECTED_CALL(xio_dowork(IGNORED_PTR_ARG));
        if (call_on_recv)
        {
            STRICT_EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));
        }
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&current_tm, sizeof(tickcounter_ms_t));
        if (call_on_recv)
        {
            STRICT_EXPECTED_CALL(gballoc_free(IGNORED_PTR_ARG));
        }
    }

    TEST_FUNCTION(tpm_comm_create_succeed)
    {
        g_htonl_value = 1;
        //arrange
        STRICT_EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(tickcounter_create());
        STRICT_EXPECTED_CALL(socketio_get_interface_description());
        STRICT_EXPECTED_CALL(xio_create(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_open(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(xio_send(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_NUM_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));

        setup_wait_to_complete_mocks(false);
        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(xio_send(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_NUM_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));

        setup_wait_to_complete_mocks(false);
        setup_wait_to_complete_mocks(true);

        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        setup_wait_to_complete_mocks(true);

        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        setup_wait_to_complete_mocks(true);

        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(socketio_get_interface_description());
        STRICT_EXPECTED_CALL(xio_create(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_open(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));

        tickcounter_ms_t init_tm = 1000;
        tickcounter_ms_t current_tm = 1010;
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&init_tm, sizeof(tickcounter_ms_t));
        STRICT_EXPECTED_CALL(xio_dowork(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&current_tm, sizeof(tickcounter_ms_t));
        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));

        STRICT_EXPECTED_CALL(xio_send(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_NUM_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        setup_wait_to_complete_mocks(false);
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&init_tm, sizeof(tickcounter_ms_t));
        STRICT_EXPECTED_CALL(xio_dowork(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(gballoc_free(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&init_tm, sizeof(tickcounter_ms_t));
        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));

        STRICT_EXPECTED_CALL(xio_send(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_NUM_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        setup_wait_to_complete_mocks(false);
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&init_tm, sizeof(tickcounter_ms_t));
        STRICT_EXPECTED_CALL(xio_dowork(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(gballoc_free(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(tickcounter_get_current_ms(IGNORED_PTR_ARG, IGNORED_PTR_ARG)).CopyOutArgumentBuffer_current_ms(&init_tm, sizeof(tickcounter_ms_t));

        STRICT_EXPECTED_CALL(htonl(IGNORED_NUM_ARG));
        STRICT_EXPECTED_CALL(gballoc_free(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_close(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_dowork(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_destroy(IGNORED_PTR_ARG));

        //act
        TPM_COMM_HANDLE tpm_handle = tpm_comm_create();

        //assert
        ASSERT_IS_NOT_NULL(tpm_handle);
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
        tpm_comm_destroy(tpm_handle);
    }

    TEST_FUNCTION(tpm_comm_destroy_succeed)
    {
        //arrange
        TPM_COMM_HANDLE tpm_handle = tpm_comm_create();
        umock_c_reset_all_calls();

        STRICT_EXPECTED_CALL(tickcounter_destroy(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_close(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(xio_destroy(IGNORED_PTR_ARG));
        STRICT_EXPECTED_CALL(gballoc_free(IGNORED_PTR_ARG));

        //act
        tpm_comm_destroy(tpm_handle);

        //assert
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
    }

    TEST_FUNCTION(tpm_comm_destroy_handle_NULL_succeed)
    {
        //arrange

        //act
        tpm_comm_destroy(NULL);

        //assert
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
    }

    TEST_FUNCTION(tpm_comm_get_type_succeed)
    {
        //arrange

        //act
        TPM_COMM_TYPE comm_type = tpm_comm_get_type(NULL);

        //assert
        ASSERT_ARE_EQUAL(TPM_COMM_TYPE, TPM_COMM_TYPE_EMULATOR, comm_type);
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
    }

END_TEST_SUITE(tpm_comm_emulator_ut)
