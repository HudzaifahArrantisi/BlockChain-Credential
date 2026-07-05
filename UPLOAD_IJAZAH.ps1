<#
╔══════════════════════════════════════════════════════════════╗
║  UPLOAD IJAZAH - SecureChain Diploma Verifier              ║
║  Upload file → Kunci (stamp+AES) → Register ke Blockchain  ║
║                                                              ║
║  CARA PAKAI:                                                 ║
║    .\UPLOAD_IJAZAH.ps1 -File "C:\ijazah.pdf"               ║
║    .\UPLOAD_IJAZAH.ps1 -File "ijazah.pdf" -Nama "Andi"    ║
║                       -Nim "123" -Kode "UNIV-123-ANDI"    ║
╚══════════════════════════════════════════════════════════════╝
#>
param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$File,
    [string]$Nama = "",
    [string]$Nim = "",
    [string]$Kode = ""
)

$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ROOT
$env:Path = "C:\ProgramData\mingw64\mingw64\bin;$env:Path"

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     UPLOAD IJAZAH - SecureChain v2.0             ║" -ForegroundColor Cyan
Write-Host "║     File → Kunci → Blockchain                    ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ─── CEK FILE ──────────────────────────────────────────────────────────
if (-not (Test-Path $File)) {
    Write-Host "  ❌ File tidak ditemukan: $File" -ForegroundColor Red
    exit 1
}
$rawPdf = (Resolve-Path $File).Path
Write-Host "  📄 File: $rawPdf" -ForegroundColor Green

# ─── INPUT DATA MAHASISWA ─────────────────────────────────────────────
if (-not $Nama) { $Nama = Read-Host "  Nama mahasiswa" }
if (-not $Nim)  { $Nim  = Read-Host "  NIM" }
if (-not $Kode) { $Kode = Read-Host "  Kode unik (contoh: UGM-$Nim-NAMA)" }

Write-Host ""
Write-Host "  ┌─────────────────────────────────────────────┐" -ForegroundColor Gray
Write-Host "  │  Nama : $($Nama.PadRight(35))│" -ForegroundColor White
Write-Host "  │  NIM  : $($Nim.PadRight(35))│" -ForegroundColor White
Write-Host "  │  Kode : $($Kode.PadRight(35))│" -ForegroundColor White
Write-Host "  └─────────────────────────────────────────────┘" -ForegroundColor Gray

# ─── KUNCI PDF ────────────────────────────────────────────────────────
$rawPdfDir = Split-Path $rawPdf -Parent
$rawPdfBase = [System.IO.Path]::GetFileNameWithoutExtension($rawPdf)
$securedPdf = Join-Path $rawPdfDir "${rawPdfBase}_SECURED.pdf"

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║     MENGUNCI PDF...                               ║" -ForegroundColor Yellow
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Yellow
Write-Host "  Cap      : SecureChain + QR Code" -ForegroundColor White
Write-Host "  Metadata : HMAC-SHA256 (CAMPUS_SIGNING_KEY)" -ForegroundColor White
Write-Host "  Password : AES-256 = Kode Unik" -ForegroundColor White
Write-Host "  Output   : $securedPdf" -ForegroundColor White
Write-Host ""

try {
    $pdfSignature = python -c "
import sys
sys.path.insert(0, '$ROOT\\gui')
from pdf_secure import stamp_and_secure, can_open
sig = stamp_and_secure('$rawPdf', '$securedPdf', '$Kode', '$Nama', '$Nim')
assert can_open('$securedPdf', '$Kode') == True, 'Gagal buka kode benar'
assert can_open('$securedPdf', 'SALAH') == False, 'Bisa buka kode salah'
print(sig)
" 2>&1 | Select-Object -Last 1
    Write-Host "  ✅ PDF diamankan!" -ForegroundColor Green
    Write-Host "     Tanda tangan: $($pdfSignature.Substring(0, 40))..." -ForegroundColor Gray
} catch {
    Write-Host "  ❌ Gagal: $_" -ForegroundColor Red
    exit 1
}

# ─── REGISTER KE BLOCKCHAIN ───────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     REGISTER KE BLOCKCHAIN...                    ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# Generate key admin jika belum ada
if (-not (Test-Path "$ROOT\data\node_config.json")) {
    Write-Host "  [1/2] Generate key admin..." -ForegroundColor Yellow
    & "$ROOT\scdv_verifier.exe" --keygen "$ROOT\data" 2>&1 | Out-Null
    Write-Host "  ✅ Key siap" -ForegroundColor Green
}

Write-Host "  [2/2] Register ke blockchain..." -ForegroundColor Yellow
$registerResult = & "$ROOT\scdv_verifier.exe" register "$securedPdf" "$Kode" "$Nama" "$Nim" 2>&1
if ($registerResult -match "STATUS=OK") {
    Write-Host "  ✅ REGISTER BERHASIL!" -ForegroundColor Green
    $registerResult -split "`n" | ForEach-Object { Write-Host "     $_" -ForegroundColor White }
} else {
    Write-Host "  ⚠️  Hasil register:" -ForegroundColor Yellow
    $registerResult -split "`n" | ForEach-Object { Write-Host "     $_" -ForegroundColor White }
}

# ─── VERIFY ───────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     VERIFY (double-lock check)                   ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
$verifyResult = & "$ROOT\scdv_verifier.exe" verify "$securedPdf" "$Kode" 2>&1
if ($verifyResult -match "STATUS=VERIFIED") {
    Write-Host "  ✅ DOKUMEN ASLI! (Integritas + Kepemilikan lolos)" -ForegroundColor Green
} else {
    Write-Host "  ⚠️  Hasil verify:" -ForegroundColor Yellow
}
$verifyResult -split "`n" | ForEach-Object { Write-Host "     $_" -ForegroundColor White }

# ─── CEK PASSWORD ────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Magenta
Write-Host "║     CEK PASSWORD PDF                              ║" -ForegroundColor Magenta
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Magenta
$pdfCheck = python -c "
import sys
sys.path.insert(0, '$ROOT\\gui')
from pdf_secure import can_open
ok = can_open('$securedPdf', '$Kode')
fail = can_open('$securedPdf', 'PALSU123')
print(f'Buka dg kode BENAR  : {ok}')
print(f'Buka dg kode SALAH : {fail}')
" 2>&1
$pdfCheck | ForEach-Object { Write-Host "  $_" -ForegroundColor White }

# ─── VALIDATE CHAIN ───────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     VALIDATE BLOCKCHAIN                           ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
$validateResult = & "$ROOT\scdv_verifier.exe" validate 2>&1
Write-Host "  $validateResult" -ForegroundColor Green

# ─── SELESAI ────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║     ✅ SELESAI!                                   ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  File SECURED : $securedPdf" -ForegroundColor Cyan
Write-Host "  Password PDF : $Kode" -ForegroundColor Cyan
Write-Host "  Blockchain   : $ROOT\data\blockchain.json" -ForegroundColor Cyan
Write-Host ""
Write-Host "  📌 Bagikan file _SECURED.pdf ke mahasiswa." -ForegroundColor White
Write-Host "  📌 Mahasiswa buka PDF dengan password = kode unik." -ForegroundColor White
Write-Host "  📌 Verifikator cek keaslian dengan:" -ForegroundColor White
Write-Host "     scdv_verifier verify `"$securedPdf`" `"$Kode`"" -ForegroundColor Gray
Write-Host ""
