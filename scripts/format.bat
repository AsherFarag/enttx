@echo off

cd /d "%~dp0.."

if not exist build (
    cmake -S . -B build
)

cmake --build build --target EnTTx-format

exit /b %ERRORLEVEL%