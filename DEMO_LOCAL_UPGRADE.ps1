#!/usr/bin/env pwsh
<#
.SYNOPSIS
  SCDV v2.0 Consortium Upgrade Demo — off-chain vault + keystore + multi-sig
  Automates key generation, node startup, diploma registration, and verification.
#>

$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $ROOT
$EXE = Join-Path $ROOT "scdv_verifier.exe"

# ── Helpers ─────────────────────────────────────────────────────────────────

function Write-Step($Title) {
    Write-Host "`n====================================" -ForegroundColor Cyan
    Write-Host "  $Title" -ForegroundColor Cyan
    Write-Host "====================================" -ForegroundColor Cyan
}

function Write-OK($Msg) {
    Write-Host "  [OK] $Msg" -ForegroundColor Green
}

function Write-Fail($Msg) {
    Write-Host "  [FAIL] $Msg" -ForegroundColor Red
}

function Write-Info($Msg) {
    Write-Host "  [..] $Msg" -ForegroundColor Yellow
}

# ── Cleanup ─────────────────────────────────────────────────────────────────

function Stop-Nodes {
    Get-Process -Name "scdv_verifier" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Info "Stopping PID $($_.Id)..."
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
}

function Wait-ForPort($Port, $Retries=15, $Delay=1) {
    for ($i = 0; $i -lt $Retries; $i++) {
        try {
            $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/api/node/info" -UseBasicParsing -TimeoutSec 2
            return ($r.StatusCode -eq 200)
        } catch { Start-Sleep -Seconds $Delay }
    }
    return $false
}

# ── Main ────────────────────────────────────────────────────────────────────

Write-Host "`n╔══════════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║  SCDV v2.0 Consortium Upgrade Demo      ║" -ForegroundColor Yellow
Write-Host "║  Off-chain vault + Keystore + Multi-sig ║" -ForegroundColor Yellow
Write-Host "╚══════════════════════════════════════════╝" -ForegroundColor Yellow

# Cleanup any leftover processes
Stop-Nodes

# Step 1: Generate consortium keys
Write-Step "Step 1: Generate Consortium Keys (UGM, UI, ITB)"
Remove-Item -Path "$ROOT\.keystore" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$ROOT\data\offchain_vault" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$ROOT\data\blockchain.json" -Force -ErrorAction SilentlyContinue

$result = & $EXE --multi-keygen ugm ui itb 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-OK "Keys generated for ugm, ui, itb"
} else {
    Write-Fail "Keygen failed: $result"
    exit 1
}

# Step 2: Build node configs
Write-Step "Step 2: Create Node Configurations"
$nodes = @{
    ugm = @{ Port = 8545; Peers = @("127.0.0.1:8546","127.0.0.1:8547") }
    ui  = @{ Port = 8546; Peers = @("127.0.0.1:8545","127.0.0.1:8547") }
    itb = @{ Port = 8547; Peers = @("127.0.0.1:8545","127.0.0.1:8546") }
}

foreach ($label in $nodes.Keys) {
    $nodeDir = Join-Path $ROOT "data" "node_$label"
    New-Item -ItemType Directory -Path $nodeDir -Force | Out-Null

    $kpPath = Join-Path $ROOT ".keystore" "$label.key"
    if (Test-Path $kpPath) {
        $kp = Get-Content $kpPath | ConvertFrom-Json
    } else {
        Write-Fail "Keystore key not found for $label"
        exit 1
    }

    $cfg = @{
        priv_key    = $kp.priv_key
        pub_key     = $kp.pub_key
        listen_addr = "0.0.0.0:$($nodes[$label].Port)"
        seed_peers  = $nodes[$label].Peers
    }
    $cfgPath = Join-Path $nodeDir "node_config.json"
    $cfg | ConvertTo-Json | Set-Content $cfgPath -Encoding ascii
    Write-OK "Config for $label (port $($nodes[$label].Port))"
}

# Step 3: Start 3 nodes
Write-Step "Step 3: Start Consortium Nodes"
$logDir = Join-Path $ROOT "data" "logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$procs = @()

foreach ($label in $nodes.Keys) {
    $cfgPath = Join-Path $ROOT "data" "node_$label" "node_config.json"
    $logPath = Join-Path $logDir "node_$label.log"
    $p = Start-Process -FilePath $EXE -ArgumentList "--node", $cfgPath -NoNewWindow -PassThru -RedirectStandardOutput $logPath
    $procs += $p
    Write-Info "Started $label (PID $($p.Id)) -> log: $logPath"
}

Write-Info "Waiting 8 seconds for Raft stabilization..."
Start-Sleep -Seconds 8

# Step 4: Verify nodes
Write-Step "Step 4: Verify Node Readiness"
$nodeInfo = @{}
foreach ($label in $nodes.Keys) {
    $ready = Wait-ForPort -Port $nodes[$label].Port
    if ($ready) {
        try {
            $info = Invoke-RestMethod -Uri "http://127.0.0.1:$($nodes[$label].Port)/api/node/info" -UseBasicParsing
            $nodeInfo[$label] = $info
            Write-OK "$label -> role=$($info.role) term=$($info.term)"
        } catch {
            Write-Fail "$label -> no response"
        }
    } else {
        Write-Fail "$label -> not ready"
    }
}

if ($nodeInfo.Count -lt 2) {
    Write-Fail "Not enough nodes online"
    Stop-Nodes
    exit 1
}

# Step 5: Register a diploma
Write-Step "Step 5: Register Diploma (Multi-Sig)"
$sampleFile = Join-Path $ROOT "data" "sample_diploma.txt"
Set-Content -Path $sampleFile -Value "DIPLOMA: Jane Smith (UGM-2024-042) - Master of Engineering" -Encoding ascii

# Pick a leader or first node
$leaderLabel = ($nodeInfo.Keys | Where-Object { $nodeInfo[$_].role -eq "LEADER" } | Select-Object -First 1)
if (-not $leaderLabel) { $leaderLabel = ($nodes.Keys | Select-Object -First 1) }

$peerStr = ($nodes.Keys | Where-Object { $_ -ne $leaderLabel } | ForEach-Object { "127.0.0.1:$($nodes[$_].Port)" }) -join ","
$details = '{"program":"Master of Engineering","gpa":"3.92","year":"2024"}'

$result = & $EXE --propose-block $sampleFile "UGM-2024-042-SMITH" "Jane Smith" "UGM-2024-042" $details $peerStr 2>&1
Write-Host "  $result"

if ($LASTEXITCODE -eq 0) {
    Write-OK "Diploma registered with multi-sig"
} else {
    Write-Fail "Registration failed"
}

# Step 6: Verify via each node
Write-Step "Step 6: Cross-Node Verification"
foreach ($label in $nodes.Keys) {
    try {
        $sync = Invoke-RestMethod -Uri "http://127.0.0.1:$($nodes[$label].Port)/api/blockchain/sync" -UseBasicParsing
        $count = $sync.count
        if ($count -gt 0) {
            $sigs = $sync.blocks[0].validator_sigs.Count
            $dh = if ([string]::IsNullOrEmpty($sync.blocks[0].details_hash)) { "no" } else { "yes" }
            Write-OK "$label: $count block(s), details_hash=$dh, validator_sigs=$sigs"
        } else {
            Write-Fail "$label: empty chain"
        }
    } catch {
        Write-Fail "$label: sync failed"
    }
}

# Step 7: Validate chain
Write-Step "Step 7: Chain Validation"
$result = & $EXE validate 2>&1
Write-Host "  $result"

# Step 8: Show vault
Write-Step "Step 8: Off-Chain Vault Status"
$vaultDir = Join-Path $ROOT "data" "offchain_vault"
if (Test-Path $vaultDir) {
    $files = Get-ChildItem $vaultDir
    Write-OK "Vault contains $($files.Count) file(s)"
    foreach ($f in $files) {
        $data = Get-Content $f.FullName | ConvertFrom-Json
        Write-Host "    $($f.Name): student=$($data.student_name)"
    }
} else {
    Write-Fail "Vault directory missing"
}

# Step 9: Stop
Write-Step "Step 9: Cleanup"
Stop-Nodes

Write-Host "`n✅ CONSORTIUM UPGRADE DEMO COMPLETED" -ForegroundColor Green
