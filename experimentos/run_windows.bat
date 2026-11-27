@echo off
REM Roda os testes e o benchmark completo no Windows nativo.
REM Uso (a partir da raiz do repositorio):
REM   experimentos\run_windows.bat          -> compila com MSVC (cl)
REM   experimentos\run_windows.bat gcc      -> compila com MinGW-w64
cd /d "%~dp0\.."

call code\build.bat %1 || exit /b 1
cd /d "%~dp0.."
code\test_platform.exe || exit /b 1

REM registra o ambiente junto dos dados
(
  echo %DATE% %TIME%
  ver
  echo PROCESSOR_IDENTIFIER=%PROCESSOR_IDENTIFIER%
  echo NUMBER_OF_PROCESSORS=%NUMBER_OF_PROCESSORS%
) > dados\windows.ambiente.txt

code\bench.exe --out dados\windows.csv
