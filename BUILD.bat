@echo off
REM Build inti C++ (scdv_verifier.exe) dengan g++ MinGW. Tanpa CMake/Visual Studio.
cd /d "%~dp0"

set "GPP=g++"
where g++ >nul 2>nul || set "GPP=C:\ProgramData\mingw64\mingw64\bin\g++.exe"
set "OPT=C:\ProgramData\mingw64\mingw64\opt"

if not exist "%OPT%\lib\libcrypto.a" (
    echo [!] OpenSSL dev ^(libcrypto.a^) tidak ditemukan di %OPT%\lib
    echo     Sesuaikan variabel OPT di file ini bila lokasi MinGW Anda berbeda.
    pause
    exit /b 1
)

echo [*] Compiling scdv_verifier.exe ...
"%GPP%" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
  src\main.cpp src\crypto_utils.cpp src\blockchain.cpp src\document_handler.cpp ^
  -I. -Iinclude -I"%OPT%\include" ^
  "%OPT%\lib\libssl.a" "%OPT%\lib\libcrypto.a" ^
  -lws2_32 -lcrypt32 -lgdi32 -ladvapi32 -luser32 -lz ^
  -o scdv_verifier.exe

if errorlevel 1 (
    echo [!] Build GAGAL.
    pause
    exit /b 1
)

if not exist data mkdir data
echo.
echo [+] BUILD SUKSES -^> scdv_verifier.exe
echo.
pause
