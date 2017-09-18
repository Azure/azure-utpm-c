#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.11)

project(utpm)

#making a global variable to know if we are on linux, windows, or macosx.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LINUX TRUE)
    #on Linux, enable valgrind
    #these commands (MEMORYCHECK...) need to apear BEFORE include(CTest) or they will not have any effect
    find_program(MEMORYCHECK_COMMAND valgrind)
    set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --error-exitcode=1")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(MACOSX TRUE)
endif()

include (CTest)

option(use_emulator "build using the tpm emulator" ON)
option(run_e2e_tests "set run_e2e_tests to ON to run e2e tests (default is OFF)" OFF)
option(run_unittests "set run_unittests to ON to run unittests (default is OFF)" OFF)
option(skip_samples "set skip_samples to ON to skip building samples (default is OFF)[if possible, they are always built]" OFF)

#Use solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

macro(compileAsC99)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "--std=c99 ${CMAKE_C_FLAGS}")
    if (NOT IN_OPENWRT)
      set (CMAKE_CXX_FLAGS "--std=c++11 ${CMAKE_CXX_FLAGS}")
    endif()
    endif()
  else()
    set (CMAKE_C_STANDARD 99)
    set (CMAKE_CXX_STANDARD 11)
  endif()
endmacro(compileAsC99)

macro(compileAsC11)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "--std=c11 ${CMAKE_C_FLAGS}")
      set (CMAKE_C_FLAGS "-D_POSIX_C_SOURCE=200112L ${CMAKE_C_FLAGS}")
      set (CMAKE_CXX_FLAGS "--std=c++11 ${CMAKE_CXX_FLAGS}")
    endif()
  else()
    set (CMAKE_C_STANDARD 11)
    set (CMAKE_CXX_STANDARD 11)
  endif()
endmacro(compileAsC11)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

enable_testing()

compileAsC99()

set(TPM_C_INC_FOLDER ${CMAKE_CURRENT_LIST_DIR}/inc CACHE INTERNAL "this is what needs to be included if using sharedLib lib" FORCE)

add_subdirectory(./deps/c-utility)
set_platform_files(${CMAKE_CURRENT_LIST_DIR}/deps/c-utility)

set(utpm_c_files
    ./src/Marshal.c
    ./src/Memory.c
    #./src/TpmCommands.c
    #./src/TpmDevice.c
    ./src/tpm_codec.c
)

set(utpm_h_files
    ./inc/azure_utpm_c/BaseTypes.h
    ./inc/azure_utpm_c/Capabilities.h
    ./inc/azure_utpm_c/CompilerDependencies.h
    ./inc/azure_utpm_c/GpMacros.h
    ./inc/azure_utpm_c/Implementation.h
    ./inc/azure_utpm_c/Marshal_fp.h
    ./inc/azure_utpm_c/Memory_fp.h
    ./inc/azure_utpm_c/swap.h
    ./inc/azure_utpm_c/Tpm.h
    ./inc/azure_utpm_c/TPMB.h
    ./inc/azure_utpm_c/TpmBuildSwitches.h
    ./inc/azure_utpm_c/TpmError.h
    ./inc/azure_utpm_c/TpmTypes.h
    ./inc/azure_utpm_c/tpm_codec.h
    ./inc/azure_utpm_c/tpm_comm.h
)

if (${use_emulator})
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)

    set(utpm_h_files
        ${utpm_h_files}
        ./inc/azure_utpm_c/tpm_socket_comm.h
    )
    set(utpm_c_files
        ${utpm_c_files}
        ./src/tpm_comm_emulator.c
        ./src/tpm_socket_comm.c
    )
else()
    if (WIN32)
        set(utpm_c_files
            ${utpm_c_files}
            ./src/tpm_comm_win32.c
        )
    else()
        set(utpm_c_files
            ${utpm_c_files}
            ./src/tpm_comm_linux.c
        )
    endif()
endif()

include_directories(./inc)
include_directories(${SHARED_UTIL_INC_FOLDER})

IF(WIN32)
    #windows needs this define
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DGB_MEASURE_MEMORY_FOR_THIS -DGB_DEBUG_ALLOC)
ENDIF(WIN32)

add_library(utpm ${utpm_c_files} ${utpm_h_files})
target_link_libraries(utpm aziotsharedutil)

if (${use_emulator})
else()
    if (WIN32)
        target_link_libraries(utpm tbs)
    endif()
endif()

if (${run_unittests})
    add_subdirectory(tests)
endif()

if (NOT ${skip_samples})
    add_subdirectory(samples)
endif()