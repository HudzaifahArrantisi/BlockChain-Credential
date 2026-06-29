# SecureChain Diploma Verifier (SCDV)

Hybrid app: **C++17 security/blockchain core** + **Python Tkinter GUI** (display only).
OpenSSL + nlohmann/json (vendored single header at `include/nlohmann/json.hpp`).
GUI deps: `pypdf`, `reportlab`.

## Build & Run (primary: g++/MinGW — no CMake/Visual Studio)

```bash
bash build_gcc.sh          # static g++ build -> scdv_verifier.exe
# or on Windows: double-click BUILD.bat
python gui/app.py          # launch GUI (after: pip install --user pypdf reportlab)
```

The exe is built **static** (`-static -static-libstdc++ -static-libgcc` + static OpenSSL
`.a` libs) — REQUIRED on this machine because Git's MinGW `libstdc++-6.dll` on PATH is an
ABI-mismatched build that segfaults a dynamically-linked exe. Static linking sidesteps it
and makes the exe portable. OpenSSL dev libs live at `C:/ProgramData/mingw64/mingw64/opt`.

`CMakeLists.txt` is kept as an optional Linux/WSL path only; the Windows flow uses g++.

## Architecture

| File | Role |
|------|------|
| `src/main.cpp` | CLI: interactive menu AND argv mode (`register`/`verify`/`validate`) for the GUI; emits `KEY=VALUE` lines |
| `src/blockchain.cpp` | Block chain logic, JSON persistence, linked-hash validation; `verify_and_get` returns block data |
| `src/crypto_utils.cpp` | SHA-256 + AES-256-CBC via OpenSSL **EVP API** (not deprecated low-level/VLA — those caused -O2 UB) |
| `src/document_handler.cpp` | File I/O, computes file hash |
| `gui/app.py` | Tkinter GUI, two tabs (Kampus register / Verifikasi) |
| `gui/pdf_secure.py` | Visible stamp + metadata + HMAC signature + AES-256 PDF password-lock (pypdf/reportlab) |
| `gui/scdv_core.py` | subprocess bridge GUI→exe, parses `KEY=VALUE`, runs exe with `cwd=ROOT` |

Headers mirror C++ sources 1:1 in `include/`. The GUI calls the exe per-operation; the C++
core is the single source of truth for hashing, AES label encryption, and the ledger.

## Critical

**Change secrets before production:** `MASTER_KEY` in `src/main.cpp:7` (C++ AES/label) and
`CAMPUS_SIGNING_KEY` in `gui/pdf_secure.py` (PDF HMAC signature). Generate with `openssl rand -base64 32`.

## Data

Blockchain persists to `data/blockchain.json` (gitignored). Runtime-only dir, created by `build.sh` or at first use.

## Security Model

Double-lock verification:
1. **SHA-256 integrity** — file content hash must match block
2. **AES-256 ownership** — unique label encrypted at rest, decrypted on verify

Chain validation checks every block's hash and previous_hash linkage.

## Conventions

- Include paths use relative `"../include/..."` from `src/` cpp files; build passes `-I. -Iinclude`
- All crypto exceptions propagate up to `main.cpp` catch blocks
- Registration hashes the **final secured PDF** (after stamp+lock); that exact file is what gets distributed and later verified. Do NOT re-encrypt at verify time (PDF encryption is non-deterministic)
- Smoke tests: `python gui/pdf_secure.py` (PDF pipeline self-check), and `scdv_verifier.exe validate`
