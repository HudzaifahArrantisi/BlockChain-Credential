# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**SecureChain Diploma Verifier (SCDV) v3.0** — A distributed blockchain-based document verification system that prevents diploma forgery through cryptographic hashing, encryption, and Raft consensus across multiple validator nodes.

### Tech Stack
- **C++17 core**: Blockchain logic, cryptography (OpenSSL EVP), Raft consensus, HTTP API
- **Python GUI**: Tkinter desktop app + web dashboards (D3.js visualization)
- **Dependencies**: OpenSSL (EVP API), nlohmann/json (vendored single-header), pypdf, reportlab

---

## Build & Run

### Build C++ Core
```bash
# Windows (PRIMARY)
BUILD.bat                                  # MinGW g++ static build → scdv_verifier.exe

# Linux/WSL (optional CMake path)
cmake -B build && cmake --build build     # Uses CMakeLists.txt
```

**Critical**: The Windows build is **static-linked** (`-static -static-libstdc++ -static-libgcc` + static OpenSSL `.a`). This is required because Git's MinGW libstdc++-6.dll on PATH is an ABI-mismatched build that segfaults. Static linking makes the exe portable.

### Run Single-Node CLI
```bash
scdv_verifier --keygen data                              # Generate keypair
scdv_verifier register "file.pdf" "CODE-123" "Name" "ID" # Register diploma
scdv_verifier verify "file.pdf" "CODE-123"               # Verify
scdv_verifier validate                                   # Check blockchain integrity
scdv_verifier --status                                   # Node status
```

### Run Distributed Network
```bash
# 3-node cluster demo (UGM, UI, ITB validators)
powershell -ExecutionPolicy Bypass -File DEMO_3NODE.ps1
# or
powershell -ExecutionPolicy Bypass -File DEMO_10NODE.ps1

# Auto-test network
python gui/test_network.py
```

### Run GUI
```bash
python gui/app.py           # Desktop Tkinter app (requires: pip install --user pypdf reportlab)
```

### Run Web Dashboard
```bash
python gui/vis_server.py    # Starts server on http://localhost:9001
# Pages: / (D3.js topology), /dev.html (n8n-style pipeline + blockchain explorer)
```

---

## Architecture & Data Flow

### Core Pipeline
```
User Input → SHA-256 Hash → AES-256 Label Encrypt → Off-Chain Vault
→ ECDSA Multi-Sig (≥2 of 3 validators) → Raft Consensus → Blockchain Ledger
```

### Security Layers
1. **File Integrity**: SHA-256 hash (1-bit change detected)
2. **Ownership**: AES-256-CBC label encryption (unique label per document, key-based unlock)
3. **Off-Chain Vault**: Student name/ID stored separately in `data/offchain_vault/`, only hash on-chain
4. **Multi-Sig**: Blocks require ≥2 valid ECDSA signatures from 3 validators
5. **Consensus**: Raft election + log replication across all nodes
6. **Chain Validation**: Every block links to previous via SHA-256 hash

### File Structure
```
src/
├── main.cpp              CLI entry, all commands (keygen, register, verify, validate, node mode)
├── blockchain.cpp        Block/chain logic, JSON persistence, linked validation
├── crypto_utils.cpp      SHA-256 + AES-256-CBC (OpenSSL EVP API)
├── document_handler.cpp  File I/O, file hashing
├── ecdsa_utils.cpp       ECDSA secp256k1 sign/verify, keypair generation
├── keystore.cpp          Key mgmt (load/save/list, `.keystore/`)
├── node.cpp              Raft consensus, HTTP server, log replication
└── offchain_vault.cpp    Store student details in `data/offchain_vault/`, retrieve by hash

gui/
├── app.py                Tkinter GUI (2 tabs: register/verify)
├── pdf_secure.py         PDF stamping, password-locking, HMAC signature
├── scdv_core.py          subprocess bridge to C++ exe (parses KEY=VALUE output)
├── vis_server.py         Web server for dashboards
└── web/
    ├── index.html        D3.js Raft topology visualization
    ├── dev.html          n8n-style pipeline + live logs + blockchain explorer
    ├── dev.js/dev.css    Dashboard logic & styling
    └── vis.js/style.css  D3.js force simulation
```

### Key Data Files
- **`data/blockchain.json`**: Ledger (gitignored, runtime-only)
- **`.keystore/`**: Private keys (gitignored, never commit)
- **`data/offchain_vault/`**: Student details (name, NIM) stored separately
- **`data/node_config.json`**: Per-node keypair + seed peers (created by `--keygen`)

---

## Critical Implementation Details

### 1. Static Build Requirement
If building on Windows with g++, always use:
```bash
g++ -c -Iinclude -o main.o src/main.cpp
g++ ... -static -static-libstdc++ -static-libgcc -o scdv_verifier.exe
```
Dynamic linking causes segfaults due to Git's mismatched MinGW AES. CMakeLists.txt provides the Linux path; Windows uses g++ only.

### 2. OpenSSL EVP API (Mandatory)
All crypto uses OpenSSL EVP (not deprecated low-level functions or variable-length arrays). 
- **SHA-256**: `EVP_sha256()` + `EVP_DigestInit/Update/Final()`
- **AES-256-CBC**: `EVP_aes_256_cbc()` + `EVP_EncryptInit/Update/Final()` for encrypt; same for decrypt

Reason: Low-level API deprecated, VLAs cause UB at -O2. See `src/crypto_utils.cpp`.

### 3. Master Key & HMAC Secret (CHANGE BEFORE PRODUCTION)
- **C++ AES Label Key**: `src/main.cpp:7` `MASTER_KEY` — regenerate with `openssl rand -base64 32`
- **PDF Signing HMAC**: `gui/pdf_secure.py` `CAMPUS_SIGNING_KEY` — same
Currently hardcoded for demo; must become env vars or key-vault in production.

### 4. PDF Registration is Non-Deterministic
- Registration hashes the **final secured PDF** (after stamping + password-lock)
- PDF encryption is non-deterministic, so hash varies per run
- Do NOT re-encrypt at verify time; the verify file hash must match exactly
- Convention: Once a file is registered, always verify that **exact same file**

### 5. C++ ↔ GUI Bridge (subprocess mode)
- GUI calls `scdv_verifier.exe` with argv
- C++ emits `KEY=VALUE` lines (e.g., `STATUS=VERIFIED`, `HASH=abc123`)
- `gui/scdv_core.py` parses output line-by-line
- Always run exe with `cwd=ROOT` (repository root, where `data/` lives)

### 6. Raft Node Modes
- **Single-node mode**: `main.cpp` argv commands (register/verify/validate)
- **Multi-node mode** (cluster): `scdv_verifier --node dataA/node_config.json` spawns HTTP server, joins cluster via seed_peers
- Nodes auto-elect leader, replicate logs, commit blocks via consensus
- Each node runs its own blockchain copy

---

## Common Development Tasks

### Register a Document
```bash
# Single node (CLI)
scdv_verifier --keygen data
scdv_verifier register "file.pdf" "UGM-123" "John Doe" "001"

# Multi-node: Use HTTP API
curl -X POST http://localhost:8545/propose \
  -H "Content-Type: application/json" \
  -d '{"file_hash":"abc123","encrypted_label":"xyz","student_name":"John","student_id":"001"}'
```

### Verify a Document
```bash
# CLI
scdv_verifier verify "file.pdf" "UGM-123"
# Output: STATUS=VERIFIED (or STATUS=NOT_FOUND, STATUS=FAILED)

# Multi-sig verification
scdv_verifier --multi-verify "file.pdf" "UGM-123"
# Output: 3/3 signatures valid

# HTTP API
curl http://localhost:8545/api/blockchain/verify?code=UGM-123&file_hash=abc123
```

### Validate Blockchain Integrity
```bash
scdv_verifier validate
# Checks: hash linkage, multi-sig validity, block ordering
```

### Generate Keys for Validator Cluster
```bash
# Single keypair (node)
scdv_verifier --keygen dataA

# Three keypairs (UGM, UI, ITB multi-sig validators)
scdv_verifier --multi-keygen
# Stores in .keystore/ugm.key, .keystore/ui.key, .keystore/itb.key
```

### Run a 3-Node Cluster Manually
```bash
# Terminal 1: Node A (becomes leader)
scdv_verifier --keygen dataA
scdv_verifier --node dataA/node_config.json
# Listens on :8545

# Terminal 2: Node B
scdv_verifier --keygen dataB
# Edit dataB/node_config.json: add "seed_peers": ["http://127.0.0.1:8545"]
scdv_verifier --node dataB/node_config.json

# Terminal 3: Node C
scdv_verifier --keygen dataC
# Edit dataC/node_config.json: add "seed_peers": ["http://127.0.0.1:8545"]
scdv_verifier --node dataC/node_config.json

# Register via any node
curl -X POST http://localhost:8545/propose -d '...'
# Block replicated to all 3 nodes
```

### Test Network Connectivity
```bash
python gui/test_network.py
# Auto-spins up 3 nodes, tests connectivity, proposes block, verifies
```

### Check Docs
- **README.md**: Feature overview, quick start, CLI reference, troubleshooting
- **API_DOCUMENTATION.md**: PDDIKTI Python wrapper (external API, not core blockchain)
- **AGENTS.md**: CLI agent descriptions
- **CARA_PAKAI.txt**: Indonesian usage guide
- **docs/**: Step-by-step demo & flowcharts

---

## When Modifying Core C++ Code

### Adding New CLI Commands
1. Parse in `main.cpp` argv handler
2. Emit `KEY=VALUE` lines to stdout (no other output on stdout)
3. Add error handling to catch blocks (emit `ERROR=message`)
4. Test via `scdv_core.py` subprocess call

### Adding Crypto Primitives
- Always use OpenSSL EVP API (see `crypto_utils.cpp`)
- Add to `include/crypto_utils.h`, implement in `src/crypto_utils.cpp`
- Test with known test vectors

### Modifying Blockchain Storage
- `blockchain.cpp` handles JSON persistence
- Block structure: `{hash, previous_hash, student_name, student_id, file_hash, encrypted_label, signatures[], timestamp}`
- `load_from_file()` and `save_to_file()` must remain symmetric
- Chain validation via `validate_chain()` must pass after changes

### Adding Raft Features
- Raft logic in `src/node.cpp` (election, AppendEntries, RequestVote)
- HTTP endpoints in `node.cpp` map to Raft RPCs
- State machine: blockchain is the log, committed entries update blockchain
- Test: run 3-node cluster, kill leader, verify election

---

## Security Considerations

### What's Signed
- **Block structure** (minus signatures field): SHA-256 hash, then ECDSA-signed
- **Multi-Sig threshold**: ≥2 of 3 validators required to commit

### What's Encrypted
- **Label** (student name/ID) encrypted with AES-256-CBC, key stored in code (CHANGE BEFORE PROD)
- **Private keys** stored in `.keystore/` (never committed)

### What's Hashed
- **File content**: SHA-256 (immutability check)
- **Block**: SHA-256 (chain linkage)
- **Vault entry**: SHA-256 (stored on-chain, data off-chain)

### Threat Model
- **Diploma tampering**: Detected by file hash mismatch + chain validation
- **Node compromise**: Attacker cannot forge signatures without private key
- **Leader Byzantine behavior**: Minority of nodes can reject invalid blocks (Raft safety)
- **Data tampering**: Off-chain vault hashes must match on-chain hashes

### Not Covered
- Network-level attacks (no TLS in demo; add for production)
- Denial-of-service on HTTP endpoints
- Social engineering (validator key compromise)

---

## Testing

### Smoke Tests
```bash
# Verify build
scdv_verifier --keygen data

# Register & verify
scdv_verifier register "BUILD.bat" "TEST-001" "Test" "001"
scdv_verifier verify "BUILD.bat" "TEST-001"
# Expected: STATUS=VERIFIED

# Validate chain
scdv_verifier validate
# Expected: Chain valid, X blocks

# Network test
python gui/test_network.py
# Expected: All nodes connect, block commits, verify succeeds
```

### Unit Testing
- No formal test suite; tests are CLI-based or network demos
- Add pytest for `src/crypto_utils.cpp` (cross-platform EVP correctness)

---

## Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| `g++ not found` | MinGW not on PATH | Run `BUILD.bat` (auto-detects) |
| Segfault at runtime | Dynamic OpenSSL AES mismatch | Use static build only (`-static`) |
| `OpenSSL error: Init failed` | OpenSSL dev libs missing | Ensure `C:/ProgramData/mingw64/mingw64/opt/` present |
| `STATUS=NOT_FOUND` | Document not registered | Check code, use `find CODE` first |
| `STATUS=FAILED` | Hash mismatch (file edited) | Verify original PDF, not altered copy |
| Node won't connect cluster | seed_peers config wrong | Check node_config.json, ping peers first |
| GUI error (black window) | Missing pypdf/reportlab | `pip install --user pypdf reportlab` |
| Blockchain corrupt | Manual edit or crash | Delete `data/blockchain.json`, register again |
| Multi-sig fail (1/3 sigs) | Not all validators signed | Ensure `--multi-keygen` ran, keys in keystore |

---

## Performance & Scalability

- **Single node**: Register/verify in <1s (SHA-256 + AES ops)
- **3-node cluster**: Block replication ~100-500ms (HTTP overhead)
- **10-node cluster**: Slower election (Raft heartbeat logic), suitable for demo
- **Ledger size**: JSON format; ~1KB per block; 1000 blocks = ~1MB

For production: Consider SQLite ledger, TLS, key-vault integration, horizontal scaling.

---

## Environment & Dependencies

### Windows (Primary)
- **MinGW g++** (e.g., `x86_64-w64-mingw32-g++.exe` on PATH)
- **OpenSSL**: Static dev libs at `C:/ProgramData/mingw64/mingw64/opt/`
- **Python 3.8+** (for GUI)
- **nlohmann/json**: Vendored in `include/nlohmann/json.hpp`

### Linux/macOS
- **g++/clang** with C++17 support
- **OpenSSL dev** (`libssl-dev`)
- **Python 3.8+**
- CMakeLists.txt provided

### Python Packages
```bash
pip install --user pypdf reportlab pillow
```

---

## Git & Commits

- **Never commit**: `.keystore/`, `data/`, `secured/`, `*.exe`, build artifacts
- **`.gitignore`** covers these; verify before commit
- **Line ending**: CRLF on Windows (git auto-converts with `core.autocrlf=true`)

---

## References

- **OpenSSL EVP**: https://www.openssl.org/docs/man1.1.1/man3/EVP_EncryptInit.html
- **Raft Consensus**: https://raft.github.io/raft.pdf
- **ECDSA secp256k1**: OpenSSL EC_KEY API
- **nlohmann/json**: https://github.com/nlohmann/json
- **Tkinter**: Python standard library docs
- **D3.js**: https://d3js.org/

---

## Quick Reference: All CLI Commands

| Command | Args | Purpose |
|---------|------|---------|
| `--keygen` | `<dir>` | Generate keypair, save config |
| `--multi-keygen` | — | Generate 3 keypairs (UGM, UI, ITB) |
| `register` | `<file> <code> <name> <nim>` | Register diploma to blockchain |
| `verify` | `<file> <code>` | Verify diploma authenticity |
| `find` | `<code>` | Lookup by unique code |
| `find_student` | `<name> <nim>` | Lookup by name + NIM |
| `validate` | — | Check blockchain integrity |
| `--node` | `<config.json>` | Launch Raft node, join cluster |
| `--status` | — | Show node + cluster status |
| `--validators` | — | List multi-sig validators |
| `--propose-block` | `<json>` | CLI block proposal |
| `--multi-verify` | `<file> <code>` | Verify with multi-sig validation |
