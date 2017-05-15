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

static void* my_gballoc_malloc(size_t size)
{
    return malloc(size);
}

static void my_gballoc_free(void* ptr)
{
    free(ptr);
}

static int TEST_FD_VALUE = 11;
#define open my_open
static int my_open(const char *path, int oflag)
{
    return TEST_FD_VALUE;
}

#include "testrunnerswitcher.h"
#include "umock_c.h"
#include "umocktypes_charptr.h"
#include "umocktypes_stdint.h"
#include "umock_c_negative_tests.h"
#include "azure_c_shared_utility/macro_utils.h"

#define ENABLE_MOCKS
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/umock_c_prod.h"
#undef ENABLE_MOCKS

#include "azure_hub_modules/tpm_comm.h"

#ifdef __cplusplus
extern "C"
{
#endif


#ifdef __cplusplus
}
#endif

static const unsigned char* TEMP_TPM_COMMAND = (const unsigned char*)0x00012345;
static uint32_t TEMP_CMD_LENGTH = (uint32_t)128;

DEFINE_ENUM_STRINGS(UMOCK_C_ERROR_CODE, UMOCK_C_ERROR_CODE_VALUES)

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code)
{
    char temp_str[256];
    (void)snprintf(temp_str, sizeof(temp_str), "umock_c reported error :%s", ENUM_TO_STRING(UMOCK_C_ERROR_CODE, error_code));
    ASSERT_FAIL(temp_str);
}

static TEST_MUTEX_HANDLE g_testByTest;
static TEST_MUTEX_HANDLE g_dllByDll;

BEGIN_TEST_SUITE(tpm_comm_linux_ut)

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

        REGISTER_UMOCK_ALIAS_TYPE(TPM_COMM_HANDLE, void*);

        REGISTER_GLOBAL_MOCK_HOOK(gballoc_malloc, my_gballoc_malloc);
        REGISTER_GLOBAL_MOCK_FAIL_RETURN(gballoc_malloc, NULL);
        REGISTER_GLOBAL_MOCK_HOOK(gballoc_free, my_gballoc_free);
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

    TEST_FUNCTION(tpm_comm_create_succeed)
    {
        //arrange
        STRICT_EXPECTED_CALL(gballoc_malloc(IGNORED_NUM_ARG));

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

        //STRICT_EXPECTED_CALL(Deinit_TPM_Codec(IGNORED_PTR_ARG));
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

    TEST_FUNCTION(tpm_comm_submit_command_handle_NULL_fail)
    {
        //arrange

        //act
        unsigned char response[TEMP_CMD_LENGTH];
        uint32_t resp_len = TEMP_CMD_LENGTH;
        int tpm_result = tpm_comm_submit_command(NULL, TEMP_TPM_COMMAND, TEMP_CMD_LENGTH, response, &resp_len);

        //assert
        ASSERT_ARE_NOT_EQUAL(int, 0, tpm_result);
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
    }

    TEST_FUNCTION(tpm_comm_submit_command_cmd_NULL_fail)
    {
        //arrange
        TPM_COMM_HANDLE tpm_handle = tpm_comm_create();
        umock_c_reset_all_calls();

        //act
        unsigned char response[TEMP_CMD_LENGTH];
        uint32_t resp_len = TEMP_CMD_LENGTH;
        int tpm_result = tpm_comm_submit_command(tpm_handle, NULL, TEMP_CMD_LENGTH, response, &resp_len);

        //assert
        ASSERT_ARE_EQUAL(int, 0, tpm_result);
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
        tpm_comm_destroy(tpm_handle);
    }

    TEST_FUNCTION(tpm_comm_submit_command_succees)
    {
        //arrange
        TPM_COMM_HANDLE tpm_handle = tpm_comm_create();
        umock_c_reset_all_calls();

        //act
        uint32_t resp_len = 0;
        int tpm_result = tpm_comm_submit_command(tpm_handle, TEMP_TPM_COMMAND, TEMP_CMD_LENGTH, NULL, &resp_len);

        //assert
        ASSERT_ARE_EQUAL(int, 0, tpm_result);
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
        tpm_comm_destroy(tpm_handle);
    }

    TEST_FUNCTION(tpm_comm_submit_command_succees)
    {
        //arrange
        TPM_COMM_HANDLE tpm_handle = tpm_comm_create();
        umock_c_reset_all_calls();

        //act
        unsigned char response[TEMP_CMD_LENGTH];
        uint32_t resp_len = TEMP_CMD_LENGTH;
        int tpm_result = tpm_comm_submit_command(tpm_handle, TEMP_TPM_COMMAND, TEMP_CMD_LENGTH, response, &resp_len);

        //assert
        ASSERT_ARE_EQUAL(int, 0, tpm_result);
        ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

        //cleanup
        tpm_comm_destroy(tpm_handle);
    }

    END_TEST_SUITE(tpm_comm_linux_ut)
