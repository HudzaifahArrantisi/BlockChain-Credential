# SecureChain Diploma Verifier (SCDV)

Hybrid app: **C++17 security/blockchain core** + **Python Tkinter GUI** + **Python Web Dashboard**.
OpenSSL + nlohmann/json (vendored single header at `include/nlohmann/json.hpp`).
GUI deps: `pypdf`, `reportlab`, `qrcode[pil]`, `Pillow`.

## Build & Run

```bash
bash build_gcc.sh          # static g++ build -> scdv_verifier.exe
# or on Windows: double-click BUILD.bat
python gui/app.py          # launch GUI (after: pip install --user pypdf reportlab qrcode[pil] Pillow)
python gui/vis_server.py   # web dashboard at http://localhost:8080
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
| `gui/web/index.html` | Single-page dashboard: Register Diploma form + topology graph + terminal logs + node status + blockchain feed |
| `gui/web/utils/graphAdapter.js` | Converts SSE node data → vis-network node/edge format (physics, tooltips, dynamic sizing) |
| `gui/web/hooks/useVisNetwork.js` | vis-network lifecycle manager (ForceAtlas2 physics, events, stabilization) |
| `gui/web/components/NetworkGraph.js` | Main graph component — vis-network + particle overlay + state change detection + smart node placement |

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

### 1. Raft Consensus Stability (Major Overhaul)
Root cause: nodes died simultaneously during leader election due to:
- Successor timeout gap too small (only 1000ms before other followers also start elections)
- No `election_in_progress` guard — followers blindly started competing elections
- Pre-vote didn't detect higher terms — caused wasted election cycles
- VisServer removed nodes after only 3 missed polls (6s)

**Fixes in `src/node.cpp`:**
- `SUCCESSOR_STEP_MS`: 1000 → **3000** — successor has 3s head start before next follower times out
- `FRESH_START_MAX_MS`: 6000 → **8000** — wider initial randomization
- **`election_in_progress` guard** in FOLLOWER loop: if election already running and node is NOT successor, extend timeout by +5000ms instead of becoming candidate
- **`handle_request_vote`** now resets follower's `last_heartbeat_time` + sets `election_in_progress=true` on receiving any vote request — prevents followers from starting competing elections
- **Pre-vote** now checks peer response terms: if any peer has higher term, `become_follower()` immediately
- After pre-vote failure, candidate **steps down to follower** instead of looping forever as candidate

**Fixes in `gui/vis_server.py`:**
- Fail threshold: 3 → **6** (12s before removal instead of 6s)
- **Election-aware removal**: if ANY node reports `election_in_progress=true`, ALL nodes are kept — no removals during election
- Removed nodes are re-inserted on next scan if they come back

### 2. Web Dashboard — UI/UX Complete Redesign
Replaced static layout with glass-morphism design, physics-based vis-network, and smooth animations throughout.

**`gui/web/utils/graphAdapter.js`:**
- Nodes now have `physics: true` with varying `mass` (leader=3, candidate=2, follower=1.5)
- Leader uses `shape: 'star'` with glow shadow instead of circle
- Rich HTML tooltips with role badge, grid layout of term/blocks/port/status
- Dynamic sizing (leader 32, candidate 26, follower 24)
- Removed fixed position computation — organic ForceAtlas2 placement

**`gui/web/hooks/useVisNetwork.js`:**
- Full ForceAtlas2Based physics config: `gravitationalConstant`, `springLength`, `damping`, `avoidOverlap`
- BarnesHut fallback solver for distant nodes
- 200-iteration stabilization with auto-fit
- New API: `wireEvents()`, `startPhysics()`, `stopPhysics()`, `stabilize()`, `enableDrag()`, `disableDrag()`

**`gui/web/components/NetworkGraph.js`:**
- Init with physics enabled + 250 stabilization iterations
- `stabilizationIterationsDone` callback saves positions, then keeps high damping (0.7) for minor adjustments
- Smart node placement: existing nodes keep saved position, new nodes get no x/y — physics places them organically
- `_saveAllPositions()` persists positions on stabilization and dragEnd
- `refresh()` resets all positions and re-stabilizes
- Removed duplicate header pill logic (now exclusively in `render()`)

**`gui/web/index.html`:**
- **Ambient background**: 3 animated radial glows that drift slowly (20s cycle)
- **Glass morphism**: `backdrop-filter: blur()` on header, panel, controls, legend, terminal logs
- **Header**: pill hover effects with gradient overlays, smooth `animateCount()` number transitions
- **Workflow overlay**: ripple on active step dot, shimmer on completed step lines
- **Leader toast**: scale + translateX enter/exit with cubic-bezier easing
- **Waiting overlay**: animated signal bars (4 bars with staggered delay)
- **Node cards**: staggered `cardIn` animation (i*40ms), hover lift + shadow, active scale
- **Block cards**: staggered fade-in, translateX on hover, genesis/new block variants
- **Form elements**: focus glow, hover border color change, file-zone hover background
- **Buttons**: gradient overlay on hover, translateY lift, active scale feedback
- **Confetti**: multi-color (green/blue/yellow/purple), mixed shapes (circle + square)
- **Terminal logs**: dark background `rgba(0,0,0,.55)` with backdrop blur, border radius, box shadow, scrollbar hidden (scrollable via overflow), log line fade-in animation
- **Resize handle**: thicker colored indicator on hover/active
- **All cards**: eager DOM diffing (only re-render when HTML actually changes)

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

### 4. D3.js → vis-network Migration
Replaced raw D3.js force graph with **vis-network (v9.1.9)**:

| Feature | Implementation |
|---------|--------------|
| Physics engine | `forceAtlas2Based` solver, 250-iteration stabilization |
| Node shapes | Leader = star (larger, glow), Follower = dot (blue), Candidate = orange, Offline = gray |
| Edge curves | Smooth `curvedCW` with directional arrows, dynamic roundness |
| Edge colors | Green (heartbeat), Blue (replication), Yellow (vote) — randomized particles |
| Flowing particles | Canvas overlay with animated dots on leader→follower edges |
| Zoom/Pan/Drag | vis-network built-in + double-click focus, hover tooltips |
| Node tooltips | Rich HTML: role badge, Term, Blocks, Port, Node ID, Status |
| Grid dots | CSS radial-gradient background pattern |
| State change detection | Leader election / node online-offline → toast notification + visual update |

| New file | Purpose |
|----------|---------|
| `gui/web/utils/graphAdapter.js` | `toVisNodes()`, `toAllEdges()` — data conversion with physics, tooltips, dynamic sizing |
| `gui/web/hooks/useVisNetwork.js` | `create()`, `setNodes()`, `setEdges()`, physics control, events, stabilization |
| `gui/web/components/NetworkGraph.js` | `init()`, `update()`, `refresh()`, `fit()`, particle overlay, highlights, smart placement |

D3.js dependency completely removed; vis-network loaded from CDN.

## Current State (July 2026)

- C++ exe builds and runs (static MinGW) — success status `STATUS=VALID`
- GUI Tkinter app works (register + verify tabs)
- Web dashboard serves at `http://localhost:8080` via `python gui/vis_server.py`
- Web Register flow works end-to-end (stamp → encrypt → blockchain)
- Blockchain nodes run on ports 8545-8559 (3-node consortium + optional extras)
- Graph uses **vis-network** with ForceAtlas2 physics, flowing particles, state change animations
- **Raft stability improved**: successor gets 3s head start, election_in_progress prevents competing elections, pre-vote detects higher terms, non-successors extend timeout during election
- **VisServer hardened**: 6-failure threshold, election-aware removal prevention, auto-reinsertion of recovered nodes
- **UI redesigned**: glass morphism, ambient glows, terminal-style logs (scrollbar hidden), smooth micro-interactions throughout

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
