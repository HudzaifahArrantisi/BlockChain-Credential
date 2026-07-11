param(
    [int]$Count = 0
)

$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ROOT
$env:Path = "C:\ProgramData\mingw64\mingw64\bin;$env:Path"

# ── Bersihkan semua data node ─────────────────────────────────────────
Write-Host "Membersihkan node & data..." -ForegroundColor Yellow
Get-Process -Name scdv_verifier -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item "$ROOT\data" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$ROOT\data" -Force | Out-Null
Write-Host "  ✅ Semua node dimatikan, /data dibersihkan" -ForegroundColor Green
Start-Sleep -Seconds 1

$PORTS = 8545..(8545 + 200)
$LABELS = @{}
foreach ($p in $PORTS) { $LABELS[$p] = "Node $($p - 8544)" }

# ── Tanya jumlah node ────────────────────────────────────────────────
if ($Count -lt 1) {
    Write-Host "╔═══════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║     START NODES - Blockchain Cluster     ║" -ForegroundColor Cyan
    Write-Host "╚═══════════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Port range: 8545–$($PORTS[-1])" -ForegroundColor Gray
    Write-Host ""
    do {
        $input = Read-Host "  Mau jalankan berapa node? (min 1)"
    } while ($input -notmatch '^\d+$' -or [int]$input -lt 1)
    $Count = [int]$input
}

$activePorts = $PORTS[0..($Count-1)]
Write-Host "  Memulai $Count node: $($activePorts -join ', ')" -ForegroundColor Yellow

# ── Matikan node yang sudah jalan ─────────────────────────────────────
Get-Process -Name scdv_verifier -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

# ── Generate key & start tiap node ───────────────────────────────────
$peerList = $activePorts | ForEach-Object { "127.0.0.1:$_" }
$firstPort = $activePorts[0]

for ($i = 0; $i -lt $Count; $i++) {
    $port = $activePorts[$i]
    $label = $LABELS[$port]
    $nodeDir = "$ROOT\data\node_$port"
    New-Item -ItemType Directory -Path $nodeDir -Force | Out-Null
    $cfgPath = "$nodeDir\node_config.json"

    if (-not (Test-Path $cfgPath)) {
        Write-Host "  [$label] Generate keypair..." -ForegroundColor Gray
        & "$ROOT\scdv_verifier.exe" --keygen $nodeDir 2>&1 | Out-Null
    }

    # Update config: listen_addr + seed_peers
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    $cfg.listen_addr = "0.0.0.0:$port"
    $cfg.seed_peers = @($peerList | Where-Object { $_ -ne "127.0.0.1:$port" })
    $cfg | ConvertTo-Json | Set-Content $cfgPath -Encoding UTF8

    # Start node di terminal baru
    $title = "$label :$port"
    if ($i -eq 0) {
        Start-Process pwsh -ArgumentList "-NoExit", "-Command", "
            `$env:Path = 'C:\ProgramData\mingw64\mingw64\bin;`$env:Path'
            Set-Location '$ROOT'
            Write-Host '╔══════════════════════════════╗' -ForegroundColor Cyan
            Write-Host '║     $label ($port) — LEADER    ║' -ForegroundColor Cyan
            Write-Host '╚══════════════════════════════╝' -ForegroundColor Cyan
            .\scdv_verifier.exe --node '$cfgPath'
        " -WindowStyle Normal
        Write-Host "  ⏳ Menunggu Node $label ($port) jadi LEADER..." -ForegroundColor Yellow
        Start-Sleep -Seconds 3
    } else {
        Start-Process pwsh -ArgumentList "-NoExit", "-Command", "
            `$env:Path = 'C:\ProgramData\mingw64\mingw64\bin;`$env:Path'
            Set-Location '$ROOT'
            Write-Host '╔══════════════════════════════╗' -ForegroundColor Green
            Write-Host '║     $label ($port) — FOLLOWER  ║' -ForegroundColor Green
            Write-Host '╚══════════════════════════════╝' -ForegroundColor Green
            .\scdv_verifier.exe --node '$cfgPath'
        " -WindowStyle Normal
        Start-Sleep -Seconds 2
    }
    Write-Host "  ✅ [$label] Node :$port started" -ForegroundColor Green
}

# ── Tunggu Raft election ─────────────────────────────────────────────
Write-Host ""
Write-Host "⏳ Menunggu Raft election stabil..." -ForegroundColor Yellow
Start-Sleep -Seconds 5

$leaderPort = ""
for ($attempt = 1; $attempt -le 6; $attempt++) {
    Write-Host "  Polling nodes... ($attempt/6)" -ForegroundColor Gray
    foreach ($port in $activePorts) {
        try {
            $info = curl.exe -s "http://127.0.0.1:$port/api/node/info" 2>&1 | ConvertFrom-Json
            $label = $LABELS[$port]
            $roleColor = if ($info.role -eq 'LEADER') { 'Yellow' } elseif ($info.role -eq 'FOLLOWER') { 'Gray' } else { 'DarkGray' }
            Write-Host "    $label :$port → $($info.role) (term $($info.term))" -ForegroundColor $roleColor
            if ($info.role -eq 'LEADER' -and -not $leaderPort) {
                $leaderPort = $port
            }
        } catch {
            Write-Host "    $port → ❌ No response" -ForegroundColor Red
        }
    }
    if ($leaderPort) { break }
    Write-Host "    ⏳ Belum ada leader, tunggu 3 detik..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3
}

# ── Selesai ──────────────────────────────────────────────────────────
Write-Host ""
Write-Host "╔═══════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║     ✅ CLUSTER READY                       ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  Nodes running: $Count" -ForegroundColor White
Write-Host "  Leader: :$leaderPort" -ForegroundColor Yellow
Write-Host ""
Write-Host "  ➜ Buka dashboard:  python gui/vis_server.py" -ForegroundColor Cyan
Write-Host "    http://localhost:8080" -ForegroundColor Cyan
Write-Host ""
Write-Host "  ➜ Upload via CLI:" -ForegroundColor Gray
Write-Host "    scdv_verifier register ""ijazah.pdf"" ""KODE-001"" ""Nama"" ""001""" -ForegroundColor Gray
Write-Host ""
