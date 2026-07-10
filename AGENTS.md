# SecureChain Diploma Verifier (SCDV)

Hybrid app: **C++17 security/blockchain core** + **Python Tkinter GUI** + **Python Web Dashboard**.
OpenSSL + nlohmann/json (vendored single header at `include/nlohmann/json.hpp`).
GUI deps: `pypdf`, `reportlab`, `qrcode[pil]`, `Pillow`.

## Build & Run

```bash
bash build_gcc.sh          # static g++ build -> scdv_verifier.exe
# or on Windows: double-click BUILD.bat
python gui/app.py          # launch GUI (after: pip install --user pypdf reportlab qrcode[pil] Pillow)
```

exe built **static** (`-static -static-libstdc++ -static-libgcc` + static OpenSSL `.a` libs) — REQUIRED because Git's MinGW `libstdc++-6.dll` on PATH is ABI-mismatched and segfaults dynamic linking. OpenSSL dev libs at `C:/ProgramData/mingw64/mingw64/opt`.

`CMakeLists.txt` kept for Linux/WSL only; Windows uses g++.

## Architecture

| File | Role |
|------|------|
| `src/main.cpp` | CLI: interactive menu AND argv mode (`register`/`verify`/`validate`/`find`/`find_student`); emits `KEY=VALUE` lines |
| `src/blockchain.cpp` | Block chain logic, JSON persistence, linked-hash validation |
| `src/crypto_utils.cpp` | SHA-256 + AES-256-CBC via OpenSSL EVP API |
| `src/document_handler.cpp` | File I/O, computes file hash |
| `src/ecdsa_utils.cpp` | ECDSA secp256k1 key generation |
| `src/keystore.cpp` | Keypair persistence |
| `src/node.cpp` | HTTP consensus node (Raft-based, listens on ports 8545-8559) |
| `src/offchain_vault.cpp` | Off-chain encrypted storage |
| `gui/app.py` | Tkinter GUI, two tabs (Register / Verify) |
| `gui/pdf_secure.py` | Visible stamp (red badge + QR + watermark), HMAC-SHA256 metadata signature, AES-256 PDF password-lock |
| `gui/scdv_core.py` | subprocess bridge GUI→exe, parses `KEY=VALUE` lines, runs exe with `cwd=ROOT` |
| `gui/vis_server.py` | **Web dashboard server** (HTTP + SSE). Serves `gui/web/`, polls blockchain nodes, handles `/api/register` POST |
| `gui/web/index.html` | Single-page dashboard: D3.js topology graph + Register Diploma form + blockchain log |
| `gui/web/vis.js` | D3.js force-directed graph rendering |

Headers in `include/` mirror C++ sources 1:1.

## Critical Secrets

**Change before production:** `MASTER_KEY` in `src/main.cpp:7` (C++ AES/label) and `CAMPUS_SIGNING_KEY` in `gui/pdf_secure.py` (PDF HMAC). Generate with `openssl rand -base64 32`.

## Data

- `data/blockchain.json` — blockchain ledger (gitignored)
- `data/uploads/` — uploaded + secured PDFs stored here by web dashboard
- `secured/` — mirror copy + `manifest.json` for GUI app
- `.keystore/` — ECDSA keypairs per node

## Security Model

1. **SHA-256 integrity** — file content hash must match blockchain block
2. **AES-256 ownership** — unique label encrypted at rest, decrypted on verify
3. **PDF stamp + HMAC** — visual verification + digital signature in metadata
4. **AES-256 password** — secured PDF can only be opened with correct Kode Unik
5. **Blockchain chain validation** — every block hash and previous_hash linkage checked

# ─── Session History (Last Active: July 2026) ───

## What Was Done

### 1. Raft Consensus Stability
- Election timeout increased to 4000-10000ms to reduce leader flapping
- VisServer polling hardened: quick discovery every 4s, node removal after 3 consecutive failures
- `vis.js` graph: all cables connect from leader to every node (green alive, red dead)

### 2. Web Dashboard Consolidation
- Converted from `dev.html`/`dev.js`/`dev.css`/`style.css` to single `gui/web/index.html` (all CSS/JS inline + D3.js)
- Fixed bug: `<header>` with pill badges was missing from HTML, causing `Cannot set properties of null (setting 'textContent')` — header re-added with `.leader-badge .pill-label`, `.follower-badge .pill-label`, `#block-num`, `#active-num`
- Old dev files deleted

### 3. Web Register Diploma — Full Pipeline
**File: `gui/vis_server.py`** — `handle_register()` rewritten to run full security pipeline:

1. Saves original upload to `data/uploads/`
2. Calls `pdf_secure.stamp_and_secure()` — applies:
   - Visual stamp: red "SECURECHAIN VERIFIED" badge + QR code + diagonal watermark
   - HMAC-SHA256 metadata signature
   - AES-256 PDF encryption (password = Kode Unik)
3. Calls `scdv_verifier.exe register` to register secured PDF hash on blockchain
4. Saves secured PDF to `data/uploads/{hash}_secured.pdf`
5. Copies to `secured/` + writes `manifest.json`
6. Returns JSON response (no file download)

**File: `gui/web/index.html`** — upload handler updated: auto-download removed, shows success message only. The secured file lives in `data/uploads/`.

## Current State (July 2026)

- C++ exe builds and runs (static MinGW)
- GUI Tkinter app works (register + verify tabs)
- Web dashboard serves at `http://localhost:8080` via `python gui/vis_server.py`
- Web Register flow works end-to-end (stamp → encrypt → blockchain)
- Blockchain nodes run on ports 8545-8559 (3-node consortium + optional extras)

## Known Issues / Edge Cases

- PDF stamp currently applied only to **page 0** of multi-page documents
- No verify tab in web dashboard yet (must use Tkinter GUI or CLI)
- `vis_server.py` imports `pdf_secure`/`scdv_core` via `sys.path.insert(0, ...)` — fragile path hack
- No authentication on web dashboard (anyone can upload)
- Secrets (`MASTER_KEY`, `CAMPUS_SIGNING_KEY`) still use dev defaults

## Next Steps (Suggested)

1. Add web verify endpoint `POST /api/verify` (upload secured PDF + Kode Unik → check blockchain)
2. Add verify tab to web dashboard
3. Serve secured PDF download from a GET endpoint (e.g. `/api/download/{hash}`)
4. Generate production secrets
5. Add authentication to web dashboard
6. Consider multi-page stamp support

## Conventions

- Include paths use relative `"../include/..."` from `src/`; build passes `-I. -Iinclude`
- All crypto exceptions propagate to `main.cpp` catch blocks
- Registration hashes the **final secured PDF** (after stamp+lock). Do NOT re-encrypt at verify time
- Smoke tests: `python gui/pdf_secure.py` (PDF pipeline), `scdv_verifier.exe validate`
- Web dashboard server: `python gui/vis_server.py [port]`
