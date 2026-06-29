@echo off
REM Jalankan GUI SecureChain. Pastikan sudah: BUILD.bat (sekali) + install_deps.bat (sekali).
cd /d "%~dp0"

if not exist scdv_verifier.exe (
    echo [!] scdv_verifier.exe belum ada. Jalankan BUILD.bat dulu.
    pause
    exit /b 1
)

python gui\app.py
if errorlevel 1 (
    echo.
    echo [!] GUI gagal jalan. Cek apakah dependency sudah terinstall: install_deps.bat
    pause
)
