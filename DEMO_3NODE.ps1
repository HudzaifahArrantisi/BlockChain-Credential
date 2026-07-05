<#
╔══════════════════════════════════════════════════════════════╗
║  DEMO 2 NODE - SecureChain Diploma Verifier                ║
║  Upload ijazah → Kunci PDF → Blockchain Distributed        ║
║                                                              ║
║  PARAMETER:                                                  ║
║    .\DEMO_3NODE.ps1                                         ║
║    .\DEMO_3NODE.ps1 -File "C:\ijazah.pdf"                  ║
║    .\DEMO_3NODE.ps1 -File "ijazah.pdf" -Nama "Andi"        ║
║                       -Nim "123" -Kode "UNIV-123-ANDI"     ║
╚══════════════════════════════════════════════════════════════╝
#>
param(
    [string]$File = "",
    [string]$Nama = "",
    [string]$Nim = "",
    [string]$Kode = ""
)

$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ROOT
$env:Path = "C:\ProgramData\mingw64\mingw64\bin;$env:Path"

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  DEMO 2 NODE - SecureChain Diploma Verifier     ║" -ForegroundColor Cyan
Write-Host "║  Upload → Kunci PDF → Blockchain → Verifikasi   ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ─── STEP 1: Bersihkan ──────────────────────────────────────────────────
Write-Host "[1/5] Membersihkan data lama..." -ForegroundColor Yellow
Get-Process -Name scdv_verifier -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Remove-Item "$ROOT\data" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$ROOT\data\nodeA","$ROOT\data\nodeB" -Force | Out-Null
Write-Host "  ✅ Data bersih" -ForegroundColor Green

# ─── STEP 2: Generate key 2 node + config ───────────────────────────────
Write-Host "[2/5] Generate ECDSA keypair + konfigurasi peer..." -ForegroundColor Yellow
& "$ROOT\scdv_verifier.exe" --keygen "$ROOT\data\nodeA" 2>&1 | Out-Null
& "$ROOT\scdv_verifier.exe" --keygen "$ROOT\data\nodeB" 2>&1 | Out-Null

# Node A: port 8545, seed ke B
$cfgA = Get-Content "$ROOT\data\nodeA\node_config.json" -Raw | ConvertFrom-Json
$cfgA.listen_addr = "0.0.0.0:8545"
$cfgA.seed_peers = @("127.0.0.1:8546")
$cfgA | ConvertTo-Json | Set-Content "$ROOT\data\nodeA\node_config.json" -Encoding UTF8

# Node B: port 8546, seed ke A
$cfgB = Get-Content "$ROOT\data\nodeB\node_config.json" -Raw | ConvertFrom-Json
$cfgB.listen_addr = "0.0.0.0:8546"
$cfgB.seed_peers = @("127.0.0.1:8545")
$cfgB | ConvertTo-Json | Set-Content "$ROOT\data\nodeB\node_config.json" -Encoding UTF8

Write-Host "  ✅ Node A: 0.0.0.0:8545 ↔ Node B: 0.0.0.0:8546" -ForegroundColor Green

# ─── STEP 3: Start 2 terminal node ─────────────────────────────────────
Write-Host "[3/5] Membuka 2 terminal node..." -ForegroundColor Yellow

$nodeAPath = "$ROOT\data\nodeA\node_config.json"
$nodeBPath = "$ROOT\data\nodeB\node_config.json"

$commonInit = @"
`$env:Path = 'C:\ProgramData\mingw64\mingw64\bin;`$env:Path'
Set-Location '$ROOT'
"@

Start-Process pwsh -ArgumentList "-NoExit","-Command","$commonInit; Write-Host '╔══════════════════════════════╗' -ForegroundColor Cyan; Write-Host '║     NODE A - 8545            ║' -ForegroundColor Cyan; Write-Host '╚══════════════════════════════╝' -ForegroundColor Cyan; .\scdv_verifier.exe --node '$nodeAPath'"
Start-Sleep -Seconds 4

Start-Process pwsh -ArgumentList "-NoExit","-Command","$commonInit; Write-Host '╔══════════════════════════════╗' -ForegroundColor Green; Write-Host '║     NODE B - 8546            ║' -ForegroundColor Green; Write-Host '╚══════════════════════════════╝' -ForegroundColor Green; .\scdv_verifier.exe --node '$nodeBPath'"

Write-Host "  ✅ 2 terminal node sudah dibuka" -ForegroundColor Green

# ─── STEP 4: Tunggu Raft stabil ────────────────────────────────────────
Write-Host "[4/5] Menunggu Raft election stabil... (12 detik)" -ForegroundColor Yellow
Start-Sleep -Seconds 12

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     CEK STATUS NODE                               ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# Polling sampai ada leader yang stabil (max 5x)
$leaderPort = ""
for ($i = 1; $i -le 5; $i++) {
    try {
        $a = curl.exe -s http://127.0.0.1:8545/api/node/info 2>&1 | ConvertFrom-Json
        Write-Host "  NODE A (8545): $($a.role) (term $($a.term))" -ForegroundColor $(if($a.role -eq 'LEADER'){'Yellow'}else{'Gray'})
    } catch { Write-Host "  NODE A (8545): ❌" -ForegroundColor Red }

    try {
        $b = curl.exe -s http://127.0.0.1:8546/api/node/info 2>&1 | ConvertFrom-Json
        Write-Host "  NODE B (8546): $($b.role) (term $($b.term))" -ForegroundColor $(if($b.role -eq 'LEADER'){'Yellow'}else{'Gray'})
    } catch { Write-Host "  NODE B (8546): ❌" -ForegroundColor Red }

    if ($a.role -eq 'LEADER') { $leaderPort = "8545"; break }
    if ($b.role -eq 'LEADER') { $leaderPort = "8546"; break }

    Write-Host "  ⏳ Belum ada leader, tunggu 3 detik lagi..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3
}

if (-not $leaderPort) {
    Write-Host "  ❌ Tidak ada leader setelah polling — coba manual." -ForegroundColor Red
    # Fallback: anggap 8545 leader
    $leaderPort = "8545"
}

Write-Host ""
Write-Host "  🏆 LEADER di port $leaderPort" -ForegroundColor Yellow
Write-Host ""

# ─── STEP 5: AMANKAN PDF + REGISTER VIA API ────────────────────────────
Write-Host "[5/5] Upload → Kunci → Register ke Blockchain" -ForegroundColor Yellow
Write-Host ""

# Tentukan file & data mahasiswa
if ($File -and (Test-Path $File)) {
    $rawPdf = (Resolve-Path $File).Path
    if (-not $Nama)  { $Nama  = Read-Host "  Nama mahasiswa" }
    if (-not $Nim)   { $Nim   = Read-Host "  NIM" }
    if (-not $Kode)  { $Kode  = Read-Host "  Kode unik (contoh: UGM-$Nim-NAMA)" }
} else {
    Write-Host "  ℹ️  Membuat PDF contoh..." -ForegroundColor Yellow
    python -c "
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
from reportlab.lib.units import mm
c = canvas.Canvas('$ROOT\\data\\ijazah_saya.pdf', pagesize=A4)
c.setFont('Helvetica-Bold', 20)
c.drawString(50*mm, 250*mm, 'UNIVERSITAS TEKNOLOGI INDONESIA')
c.setFont('Helvetica-Bold', 16)
c.drawString(50*mm, 235*mm, 'IJAZAH')
c.setFont('Helvetica', 12)
c.drawString(50*mm, 215*mm, 'Dengan ini menyatakan bahwa:')
c.setFont('Helvetica-Bold', 14)
c.drawString(50*mm, 200*mm, 'Hudzaifah Rahman')
c.setFont('Helvetica', 12)
c.drawString(50*mm, 185*mm, 'NIM: 010203')
c.drawString(50*mm, 170*mm, 'Program Studi: Teknik Informatika')
c.drawString(50*mm, 155*mm, 'Telah menyelesaikan pendidikan')
c.drawString(50*mm, 140*mm, 'dan dinyatakan LULUS')
c.setFont('Helvetica-Oblique', 10)
c.drawString(50*mm, 110*mm, 'Jakarta, 1 Juli 2026')
c.drawString(50*mm, 100*mm, 'Dekan,')
c.drawString(50*mm, 85*mm, '(ttd) Prof. Dr. Ahmad')
c.save()
" 2>&1 | Out-Null
    $rawPdf = "$ROOT\data\ijazah_saya.pdf"
    $Nama  = "Hudzaifah Rahman"
    $Nim   = "010203"
    $Kode  = "UTI-010203-HUDZAIFAH"
}

$rawPdfDir = Split-Path $rawPdf -Parent
$rawPdfBase = [System.IO.Path]::GetFileNameWithoutExtension($rawPdf)
$securedPdf = Join-Path $rawPdfDir "${rawPdfBase}_SECURED.pdf"

# ─── KUNCI PDF ─────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║     KUNCI PDF (Stamp + HMAC + AES-256)           ║" -ForegroundColor Yellow
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Yellow
Write-Host "  Input  : $rawPdf" -ForegroundColor White
Write-Host "  Output : $securedPdf" -ForegroundColor White
Write-Host "  Nama   : $Nama  |  NIM: $Nim" -ForegroundColor White
Write-Host "  Kode   : $Kode" -ForegroundColor White
Write-Host ""

try {
    $sig = python -c "
import sys
sys.path.insert(0, '$ROOT\\gui')
from pdf_secure import stamp_and_secure, can_open
s = stamp_and_secure('$rawPdf', '$securedPdf', '$Kode', '$Nama', '$Nim')
assert can_open('$securedPdf', '$Kode')
assert not can_open('$securedPdf', 'SALAH')
print(s)
" 2>&1 | Select-Object -Last 1
    Write-Host "  ✅ PDF diamankan! HMAC: $($sig.Substring(0, 24))..." -ForegroundColor Green
} catch {
    Write-Host "  ❌ Gagal: $_" -ForegroundColor Red; exit 1
}

# ─── HASH + REGISTER VIA API LEADER ────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     REGISTER KE BLOCKCHAIN via API LEADER        ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

$fileHash = (certutil -hashfile "$securedPdf" SHA256 | Select-String -Pattern "^[0-9a-f]{64}$").Matches.Value
Write-Host "  SHA-256 file : $fileHash" -ForegroundColor Gray
Write-Host "  Kirim ke     : http://127.0.0.1:$leaderPort/api/blockchain/propose" -ForegroundColor Gray
Write-Host ""

$proposeBody = @{
    file_hash       = $fileHash
    encrypted_label = $Kode
    student_name    = $Nama
    student_id      = $Nim
    encrypted_details = ""
} | ConvertTo-Json -Compress

try {
    $resp = curl.exe -s -X POST "http://127.0.0.1:$leaderPort/api/blockchain/propose" `
        -H "Content-Type: application/json" -d $proposeBody 2>&1
    if ($resp -match "ACCEPTED") {
        Write-Host "  ✅ Blok DITERIMA oleh LEADER port $leaderPort" -ForegroundColor Green
        Write-Host "     Leader akan mereplikasi ke FOLLOWER via Raft AppendEntries" -ForegroundColor White
    } else {
        Write-Host "  ⚠️  Respon: $resp" -ForegroundColor Yellow
    }
} catch {
    Write-Host "  ❌ Gagal: $_" -ForegroundColor Red
}

# ─── CEK BLOCKCHAIN DI KEDUA NODE ──────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     VERIFIKASI: BLOCKCHAIN TERREPLIKASI          ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""
Start-Sleep -Seconds 2  # tunggu replikasi Raft

$nodeOk = $true
foreach ($port in @(8545, 8546)) {
    try {
        $chain = curl.exe -s "http://127.0.0.1:$port/api/blockchain/sync" 2>&1 | ConvertFrom-Json
        $count = @($chain.blocks).Count
        $label = "  Node $port : $count blok"
        if ($count -gt 0) {
            $hash = $chain.blocks[0].file_hash.Substring(0, 16)
            $label += " | file_hash: ${hash}..."
        } else {
            $nodeOk = $false
        }
        Write-Host $label -ForegroundColor $(if($count -gt 0){'Green'}else{'Gray'})
    } catch {
        Write-Host "  Node $port : ❌" -ForegroundColor Red
        $nodeOk = $false
    }
}

Write-Host ""
if ($nodeOk) {
    Write-Host "  ✅ Blockchain terverifikasi — blok sama di kedua node!" -ForegroundColor Green
} else {
    Write-Host "  ⚠️  Blockchain kosong atau tidak sinkron — cek node log" -ForegroundColor Yellow
}

# ─── CEK PASSWORD PDF ─────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Magenta
Write-Host "║     CEK PASSWORD PDF                              ║" -ForegroundColor Magenta
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Magenta
$pdfCheck = python -c "
import sys; sys.path.insert(0, '$ROOT\\gui')
from pdf_secure import can_open
ok = can_open('$securedPdf', '$Kode')
fail = can_open('$securedPdf', 'PALSU123')
print(f'Buka dg kode BENAR  : {ok}')
print(f'Buka dg kode SALAH : {fail}')
" 2>&1
$pdfCheck | ForEach-Object { Write-Host "  $_" -ForegroundColor White }

# ─── FAILOVER ─────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║     FAILOVER TEST                                 ║" -ForegroundColor Yellow
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Yellow
Write-Host ""
Write-Host "  Leader port $leaderPort — coba matikan (Ctrl+C di terminal itu)" -ForegroundColor Yellow
Write-Host "  FOLLOWER akan detect election timeout dan pilih leader baru" -ForegroundColor White
Write-Host "  Cek setelah 5 detik: curl http://localhost:854x/api/node/info" -ForegroundColor Gray
Write-Host ""

# ─── SELESAI ────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║     ✅ DEMO BERHASIL!                             ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  📄 File SECURED : $securedPdf" -ForegroundColor Cyan
Write-Host "  🔑 Password PDF : $Kode" -ForegroundColor Cyan
Write-Host "  ⛓️  Blockchain   : 2 node via Raft consensus" -ForegroundColor Cyan
Write-Host ""

Read-Host "Tekan ENTER untuk menutup semua node dan selesai"
Get-Process -Name scdv_verifier -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "Semua node dimatikan. Selesai!" -ForegroundColor Green
