@echo off
REM Install dependency Python untuk GUI (sekali saja).
echo [*] Menginstall pypdf + reportlab ...
python -m pip install --user pypdf reportlab
echo.
echo [+] Selesai. Jalankan GUI dengan: RUN.bat
pause
