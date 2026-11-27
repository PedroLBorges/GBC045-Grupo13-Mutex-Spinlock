@echo off
REM Compila no Windows NATIVO, usando apenas a API Win32 (sem winpthreads).
REM
REM Opcao 1 (MSVC): abra o "x64 Native Tools Command Prompt for VS" e rode build.bat
REM Opcao 2 (MinGW-w64): rode "build.bat gcc" (nao passa -pthread: so <windows.h>)
cd /d "%~dp0"

if /i "%1"=="gcc" goto mingw

cl /nologo /O2 /W3 /Fe:bench.exe bench.c platform_thread.c || exit /b 1
cl /nologo /O2 /W3 /I. /Fe:test_platform.exe ..\testes\test_platform.c platform_thread.c || exit /b 1
del *.obj >nul 2>&1
goto done

:mingw
gcc -O2 -Wall -Wextra -o bench.exe bench.c platform_thread.c || exit /b 1
gcc -O2 -Wall -Wextra -I. -o test_platform.exe ..\testes\test_platform.c platform_thread.c || exit /b 1

:done
echo ok: bench.exe e test_platform.exe gerados
