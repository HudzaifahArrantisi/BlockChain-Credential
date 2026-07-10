---
name: securechain-project-status
description: SecureChain Diploma Verifier (SCDV) - comprehensive project status and architecture
metadata:
  type: project
  last_updated: 2026-07-10
---

# SecureChain Diploma Verifier (SCDV) - Project Context

## Project Overview
**Hybrid Application**: Blockchain-based diploma verification system with C++17 core, Python Tkinter GUI, and Python web dashboard.

## Current Architecture (July 2026)

### Core Components
1. **C++ Blockchain Core** (`src/`)
   - `main.cpp` - CLI interface (interactive menu + argv mode)
   - `blockchain.cpp` - Block chain logic, JSON persistence
   - `crypto_utils.cpp` - SHA-256 + AES-256-CBC (OpenSSL EVP)
   - `node.cpp` - Raft consensus node (HTTP server, ports 8545-8559)
   - `ecdsa_utils.cpp` - ECDSA secp256k1 key generation
   - `document_handler.cpp` - File I/O and hashing
   - `keystore.cpp` - Key management
   - `offchain_vault.cpp` - Encrypted student data storage

2. **Python GUI** (`gui/`)
   - `app.py` - Tkinter desktop app (Register/Verify tabs)
   - `pdf_secure.py` - PDF stamping, password-locking, HMAC signature
   - `scdv_core.py` - subprocess bridge to C++ exe

3. **Web Dashboard** (`gui/web/` + `gui/vis_server.py`)
   - `vis_server.py` - Python web server (HTTP + SSE)
   - `index.html` - Single-page dashboard with D3.js graph
   - `vis.js` - D3.js force-directed graph + animated cables
   - Real-time node polling and blockchain visualization

## Recent Work (This Session)

### 1. Fixed `DEFAULT_NODES` Error
- **Issue**: `NameError: name 'DEFAULT_NODES' is not defined` in vis_server.py
- **Fix**: Added `DEFAULT_NODES = [{"port": p, "label": NODE_LABELS.get(p, f"Node {p}")} for p in PORT_RANGE]`
- **File**: `gui/vis_server.py` line 31

### 2. Implemented Animated Cables (NEW FEATURE!)
- **What**: Visual animated cables connecting all Follower nodes to Leader node
- **Where**: `gui/web/vis.js`
- **Implementation**:
  - Dual-layer visualization:
    - Background glow layer (8px width, 0.25 opacity)
    - Main animated cable (3.5px width, 0.85 opacity)
  - CSS @keyframes animation (`cable-flow`, 1.2s/cycle)
  - SVG filter `linkGlow` for blur effect
  - Global variables: `linkPath`, `linkBgPath`, `linkLabelText`

- **Visibility Improvements**:
  - Stroke width: 2.5px → 3.5px (main cable)
  - Stroke width: 6px → 8px (background glow)
  - Opacity: 0.75 → 0.85 (main cable)
  - Opacity: 0.15 → 0.25 (background glow)

### 3. Created Test & Documentation
- `gui/web/test-cables.html` - Demo page showing animated cables (no nodes needed)
- `KABEL_ANIMASI_GUIDE.md` - Comprehensive user guide with troubleshooting
- `ANIMATED_CABLES.md` - Technical implementation details

## Known Issues & Limitations

### Current Issues
1. PDF stamp applied only to page 0 (multi-page support missing)
2. No verify tab in web dashboard (must use Tkinter GUI or CLI)
3. Path hack in vis_server.py: `sys.path.insert(0, ...)` - fragile
4. No authentication on web dashboard
5. Hardcoded secrets: `MASTER_KEY` (src/main.cpp:7), `CAMPUS_SIGNING_KEY` (gui/pdf_secure.py)

### Render Issues
- Animated cables might not render if browser cache not cleared (hard refresh needed)
- Cables only visible when LEADER node exists
- Opacity/visibility depends on node count and graph zoom level

## Build & Runtime

### Windows Build
```bash
BUILD.bat  # or: cd src && g++ -static -static-libstdc++ -static-libgcc ...
```

### Run Dashboard
```bash
python gui/vis_server.py
# Access: http://127.0.0.1:8080
```

### Run Nodes
```bash
python gui/test_network.py  # Auto-spawn 3-node cluster
# or manual: scdv_verifier --node data/node_config.json
```

## Next Steps (Suggested)

1. **Web Verify Endpoint** - `POST /api/verify` (upload + verify on blockchain)
2. **Web Verify Tab** - UI for diploma verification in dashboard
3. **Secured PDF Download** - `GET /api/download/{hash}`
4. **Production Secrets** - Generate new `MASTER_KEY` and `CAMPUS_SIGNING_KEY`
5. **Authentication** - Add user authentication to web dashboard
6. **Multi-Page Stamp** - Support PDF stamp on multiple pages

## Key Files to Remember

| File | Purpose | Modified This Session |
|------|---------|-------|
| `gui/vis_server.py` | Web dashboard server | Yes (DEFAULT_NODES fix) |
| `gui/web/vis.js` | D3.js graph visualization | Yes (animated cables) |
| `gui/web/test-cables.html` | Animated cables demo | Yes (created) |
| `KABEL_ANIMASI_GUIDE.md` | User guide | Yes (created) |
| `src/main.cpp` | CLI + blockchain core | No |
| `gui/app.py` | Tkinter GUI | No |

## Testing URLs

- **Dashboard**: http://127.0.0.1:8080
- **Animated Cables Test**: http://127.0.0.1:8080/test-cables.html
- **API State**: http://127.0.0.1:8080/api/state

## Important Notes for Continuation

1. **Visibility Issue**: If animated cables don't show after changes:
   - Hard refresh: Ctrl+Shift+R
   - Check F12 Elements for `<path class="animated-link">`
   - Verify nodes running (check `/api/state`)

2. **CSS Animation**: Uses `@keyframes cable-flow` in `<style>` inside SVG `<defs>`
   - GPU-accelerated, no JavaScript timers
   - Seamless infinite loop

3. **D3.js Force Simulation**: Cables position depends on node coordinates
   - Links rendered in tick function
   - Must update both `linkPath` and `linkBgPath`

4. **Real-time Updates**: SSE stream from `/events` endpoint
   - Updates every 2 seconds
   - Auto-discovery of new nodes
   - Node removal after 3 consecutive failures

---

**Session Date**: 2026-07-10  
**Prepared for**: Claude Code Desktop (Opus 4.8, 1M context)  
**Status**: Ready for continuation ✅
