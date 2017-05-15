@REM Copyright (c) Microsoft. All rights reserved.
@REM Licensed under the MIT license. See LICENSE file in the project root for full license information.

@setlocal EnableExtensions EnableDelayedExpansion
@echo off

set current-path=%~dp0
rem // remove trailing slash
set current-path=%current-path:~0,-1%

echo Current Path: %current-path%

set build-root=%current-path%\..
rem // resolve to fully qualified path
for %%i in ("%build-root%") do set build-root=%%~fi

set repo_root=%build-root%
rem // resolve to fully qualified path
for %%i in ("%repo_root%") do set repo_root=%%~fi

set CMAKE_DIR=tpm_win32
set build-config=Debug
set build-platform=Win32

echo Build Root: %build-root%
echo Repo Root: %repo_root%

echo CMAKE Output Path: %build-root%\cmake\%CMAKE_DIR%

if EXIST %build-root%\cmake\%CMAKE_DIR% (
    rmdir /s/q %build-root%\cmake\%CMAKE_DIR%
    rem no error checking
)

echo %build-root%\cmake\%CMAKE_DIR%
mkdir %build-root%\cmake\%CMAKE_DIR%
rem no error checking
pushd %build-root%\cmake\%CMAKE_DIR%

cmake %build-root% -Drun_unittests:BOOL=ON 

:build-a-solution
call :_run-msbuild "Build" %1 %2 %3
goto :eof

:run-unit-tests
call :_run-tests %1 "UnitTests"
goto :eof

:_run-msbuild
rem // optionally override configuration|platform
setlocal EnableExtensions
set build-target=utpm.sln

echo %build-target%

msbuild /m %build-target% "/p:Configuration=%build-config%;Platform=%build-platform%"
if not !ERRORLEVEL!==0 exit /b !ERRORLEVEL!
goto :eof

:_run-tests
rem // discover tests
set test-dlls-list=
set test-dlls-path=%build-root%\%~1\build\windows\%build-platform%\%build-config%
for /f %%i in ('dir /b %test-dlls-path%\*%~2*.dll') do set test-dlls-list="%test-dlls-path%\%%i" !test-dlls-list!

if "%test-dlls-list%" equ "" (
    echo No unit tests found in %test-dlls-path%
    exit /b 1
)

rem // run tests
echo Test DLLs: %test-dlls-list%
echo.
vstest.console.exe %test-dlls-list%

if not !ERRORLEVEL!==0 exit /b !ERRORLEVEL!
goto :eof

echo done