<#
╔══════════════════════════════════════════════════════════════╗
║  DEMO 10 NODE - SecureChain Diploma Verifier               ║
║  Full cluster: A(8545) - J(8554) with Raft consensus      ║
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

$LABELS = @("A","B","C","D","E","F","G","H","I","J")
$PORTS = @(8545,8546,8547,8548,8549,8550,8551,8552,8553,8554)
$NODE_COUNT = $PORTS.Count

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  DEMO $NODE_COUNT NODE - SecureChain Cluster       ║" -ForegroundColor Cyan
Write-Host "║  Raft Consensus + Blockchain Distributed         ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ─── STEP 1: Clean ──────────────────────────────────────────────────
Write-Host "[1/5] Membersihkan data lama..." -ForegroundColor Yellow
Get-Process -Name scdv_verifier -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Remove-Item "$ROOT\data" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$ROOT\data" -Force | Out-Null
Write-Host "  ✅ Data bersih" -ForegroundColor Green

# ─── STEP 2: Generate keys + configs ────────────────────────────────
Write-Host "[2/5] Generate ECDSA keypairs + konfigurasi $NODE_COUNT node..." -ForegroundColor Yellow

$nodePaths = @{}
$seedPorts = $PORTS -join '", "'

for ($i = 0; $i -lt $NODE_COUNT; $i++) {
    $label = $LABELS[$i]
    $port = $PORTS[$i]
    $dir = "$ROOT\data\node$label"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    & "$ROOT\scdv_verifier.exe" --keygen "$dir" 2>&1 | Out-Null

    $cfg = Get-Content "$dir\node_config.json" -Raw | ConvertFrom-Json
    $cfg.listen_addr = "0.0.0.0:$port"
    # Seed to all other nodes
    $others = @()
    for ($j = 0; $j -lt $NODE_COUNT; $j++) {
        if ($j -ne $i) { $others += "127.0.0.1:$($PORTS[$j])" }
    }
    $cfg.seed_peers = $others
    $cfg | ConvertTo-Json | Set-Content "$dir\node_config.json" -Encoding UTF8

    $nodePaths[$port] = $dir
    Write-Host "  Node $label : 0.0.0.0:$port" -ForegroundColor Gray
}

$allPortsStr = $PORTS -join ", "
Write-Host "  ✅ $NODE_COUNT node siap: $allPortsStr" -ForegroundColor Green

# ─── STEP 3: Start all node terminals ───────────────────────────────
Write-Host "[3/5] Membuka $NODE_COUNT terminal node..." -ForegroundColor Yellow

$commonInit = @"
`$env:Path = 'C:\ProgramData\mingw64\mingw64\bin;`$env:Path'
Set-Location '$ROOT'
"@

$nodeColors = @("Cyan","Green","Yellow","Magenta","Red","Blue","White","Cyan","Green","Yellow")
for ($i = 0; $i -lt $NODE_COUNT; $i++) {
    $label = $LABELS[$i]
    $port = $PORTS[$i]
    $cfgPath = "$ROOT\data\node$label\node_config.json"
    $color = $nodeColors[$i % $nodeColors.Length]

    Start-Process pwsh -ArgumentList "-NoExit","-Command","$commonInit; Write-Host '╔══════════════════╗' -ForegroundColor $color; Write-Host '║ NODE $label - $port         ║' -ForegroundColor $color; Write-Host '╚══════════════════╝' -ForegroundColor $color; .\scdv_verifier.exe --node '$cfgPath'"

    # Stagger start to avoid port/connection race
    Start-Sleep -Milliseconds 1500
}

Write-Host "  ✅ $NODE_COUNT terminal node dibuka" -ForegroundColor Green

# ─── STEP 4: Wait for Raft ──────────────────────────────────────────
Write-Host "[4/5] Menunggu Raft election stabil... (60 detik)" -ForegroundColor Yellow
Start-Sleep -Seconds 60

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     CEK STATUS NODE                               ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

$leaderPort = ""
for ($attempt = 1; $attempt -le 12; $attempt++) {
    $leaderPort = ""
    $allOnline = $true
    Write-Host "  ─── Polling ke-$attempt ───" -ForegroundColor DarkGray

    for ($i = 0; $i -lt $NODE_COUNT; $i++) {
        $label = $LABELS[$i]
        $port = $PORTS[$i]
        try {
            $info = curl.exe -s "http://127.0.0.1:$port/api/node/info" 2>&1 | ConvertFrom-Json
            $roleStr = $info.role
            $termStr = $info.term
            if ($roleStr -eq 'LEADER') { $leaderPort = $port }
            Write-Host "  $label ($port): $roleStr (term $termStr)" -ForegroundColor $(if($roleStr -eq 'LEADER'){'Yellow'}elseif($roleStr -eq 'FOLLOWER'){'Gray'}else{'Red'})
        } catch {
            Write-Host "  $label ($port): ❌" -ForegroundColor Red
            $allOnline = $false
        }
    }

    if ($leaderPort -and $allOnline) {
        Write-Host "" -ForegroundColor DarkGray
        Write-Host "  🏆 LEADER di port $leaderPort — semua node online!" -ForegroundColor Yellow
        break
    }

    if ($leaderPort -and !$allOnline) {
        Write-Host "  ⏳ Leader terpilih tapi belum semua node online..." -ForegroundColor Yellow
    } elseif (!$leaderPort) {
        Write-Host "  ⏳ Belum ada leader..." -ForegroundColor Yellow
    }
    Start-Sleep -Seconds 5
}

if (-not $leaderPort) {
    Write-Host "  ⚠️  Tidak ada leader — coba manual. Fallback ke 8545." -ForegroundColor Red
    $leaderPort = "8545"
}

# ─── STEP 5: PDF + Register + Verify ────────────────────────────────
Write-Host ""
Write-Host "[5/5] Upload → Kunci → Register ke Blockchain" -ForegroundColor Yellow
Write-Host ""

if ($File -and (Test-Path $File)) {
    $rawPdf = (Resolve-Path $File).Path
    if (-not $Nama)  { $Nama = "Mahasiswa" }
    if (-not $Nim)   { $Nim  = "000000" }
    if (-not $Kode)  { $Kode = "UNIV-000000-MAHASISWA" }
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

# ─── SECURE PDF ─────────────────────────────────────────────────────
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

# ─── REGISTER VIA API LEADER ────────────────────────────────────────
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
        Write-Host "     Leader mereplikasi ke $(($NODE_COUNT-1)) FOLLOWER via Raft AppendEntries" -ForegroundColor White
    } else {
        Write-Host "  ⚠️  Respon: $resp" -ForegroundColor Yellow
    }
} catch {
    Write-Host "  ❌ Gagal: $_" -ForegroundColor Red
}

# ─── VERIFY ALL NODES ───────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     VERIFIKASI: BLOCKCHAIN TERREPLIKASI          ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""
Start-Sleep -Seconds 5

$nodeOk = $true
$replicatedCount = 0
for ($i = 0; $i -lt $NODE_COUNT; $i++) {
    $label = $LABELS[$i]
    $port = $PORTS[$i]
    try {
        $chain = curl.exe -s "http://127.0.0.1:$port/api/blockchain/sync" 2>&1 | ConvertFrom-Json
        $count = @($chain.blocks).Count
        if ($count -gt 0) {
            $hash = $chain.blocks[0].file_hash.Substring(0, 16)
            $replicatedCount++
            Write-Host "  $label ($port): $count blok | $hash..." -ForegroundColor Green
        } else {
            Write-Host "  $label ($port): 0 blok ❌" -ForegroundColor Red
            $nodeOk = $false
        }
    } catch {
        Write-Host "  $label ($port): ❌" -ForegroundColor Red
        $nodeOk = $false
    }
}

Write-Host ""
if ($nodeOk) {
    Write-Host "  ✅ Semua $NODE_COUNT node sinkron — blok sama!" -ForegroundColor Green
} else {
    Write-Host "  ⚠️  $replicatedCount/$NODE_COUNT node tereplikasi — cek log" -ForegroundColor Yellow
}

# ─── CHECK PDF PASSWORD ─────────────────────────────────────────────
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

# ─── DONE ───────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║     ✅ DEMO $NODE_COUNT NODE BERHASIL!             ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  📄 File SECURED : $securedPdf" -ForegroundColor Cyan
Write-Host "  🔑 Password PDF : $Kode" -ForegroundColor Cyan
Write-Host "  ⛓️  Blockchain   : $NODE_COUNT node via Raft consensus" -ForegroundColor Cyan
Write-Host "  🌐 Dashboard    : http://127.0.0.1:8080/" -ForegroundColor Cyan
Write-Host ""

Read-Host "Tekan ENTER untuk menutup semua node dan selesai"
Get-Process -Name scdv_verifier -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "Semua node dimatikan. Selesai!" -ForegroundColor Green
