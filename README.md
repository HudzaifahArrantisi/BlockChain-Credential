# SecureChain Diploma Verifier (SCDV) v3.0

**Sistem Verifikasi Ijazah Blockchain Terdistribusi** — Cegah Pemalsuan Dokumen dengan Raft Consensus + ECDSA Multi-Sig + Off-Chain Vault

---

## 📌 Buat Apa Ini?

Mencegah pemalsuan ijazah. Setiap dokumen melewati **6 tahap pengamanan**:

1. **📄 Upload** — File PDF diterima sistem
2. **🔐 SHA-256 Hash** — File di-hash, 1 bit perubahan → hash berbeda → ketahuan
3. **🔑 AES-256 Encrypt** — Label unik dienkripsi, hanya pemilik kunci bisa baca
4. **🗄️ Off-Chain Vault** — Data sensitif (nama, NIM) disimpan terpisah, hanya hash-nya di blockchain
5. **✍️ Multi-Sig ECDSA** — Blok ditandatangani ≥2 dari 3 validator (UGM/UI/ITB)
6. **⛓️ Block Commit** — Blok dicatat di blockchain via Raft consensus, direplikasi ke semua node

---

## 🚀 Quick Start (30 Detik)

```cmd
BUILD.bat                          # Build C++ → scdv_verifier.exe
scdv_verifier --keygen data        # Generate keypair (sekali)
scdv_verifier register "ijazah.pdf" "UGM-001" "Hudzaifah" "001"
scdv_verifier verify "ijazah.pdf" "UGM-001"
```

Kalau output `STATUS=VERIFIED` → ASLI. Kalau error → PALSU/DIUBAH.

---

## 📋 Fitur Lengkap

### CLI — 15+ Perintah

| Perintah | Fungsi |
|----------|--------|
| `--keygen <dir>` | Generate ECDSA keypair + simpan ke keystore |
| `--multi-keygen` | Generate 3 keypair (UGM, UI, ITB) untuk multi-sig |
| `register <file> <kode> <nama> <nim>` | Daftarkan ijazah ke blockchain |
| `verify <file> <kode>` | Verifikasi keaslian file |
| `find <kode_unik>` | Cari data berdasarkan kode unik |
| `find_student <nama> <nim>` | Cari berdasarkan nama + NIM |
| `validate` | Validasi integritas seluruh blockchain |
| `--node <config>` | Jalankan node Raft (HTTP server) |
| `--status` | Lihat status node + cluster |
| `--validators` | Lihat daftar validator multi-sig |
| `--propose-block <json>` | Proposal blok baru via CLI |
| `--multi-verify <file> <kode>` | Verifikasi dengan validasi multi-sig |

### GUI — Tkinter Desktop

```cmd
python gui/app.py
```

2 tab:
- **Kampus** — Daftarkan ijazah (pilih PDF, isi data, klik amankan)
- **Verifikasi** — Upload file SECURED, masukkan kode, cek keaslian

### Web Dashboard — Real-Time Visualisasi

```cmd
python gui/vis_server.py
# Buka http://localhost:9001
```

3 tampilan:
- **`dev.html`** — Dashboard monitoring (node cards, live logs, action feed, blockchain view)
- **`index.html`** — Visualisasi D3.js (node topology, Raft election, particle flow)
- Pipeline n8n-style: Upload → Hash → Encrypt → Vault → Multi-Sig → Commit

### Distributed Network — Multi-Node Raft

Jalankan cluster dengan 3, 5, atau 10 node:

```cmd
# Demo 3 node (UGM, UI, ITB)
powershell -ExecutionPolicy Bypass -File DEMO_3NODE.ps1

# Demo 10 node
powershell -ExecutionPolicy Bypass -File DEMO_10NODE.ps1

# Test konektivitas jaringan
python gui/test_network.py
```

Test otomatis:
```cmd
python gui/test_network.py
# Output: Testing 3 nodes... ✓ All nodes connected
#          Proposing block... ✓ Block committed
#          Verifying... ✓ Document verified
```

---

## 🔒 Arsitektur Keamanan

```
                    ┌─────────────────────────────┐
                    │      USER (Upload PDF)       │
                    └──────────────┬──────────────┘
                                   ▼
                    ┌─────────────────────────────┐
                    │  ① SHA-256 File Hash        │
                    │  ② AES-256 Label Encrypt    │
                    └──────────────┬──────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                    OFF-CHAIN VAULT                               │
│  data/offchain_vault/<details_hash>.json                         │
│  { "student_name": "...", "student_id": "..." }                 │
│  Hanya hash-nya (SHA-256) yang disimpan di blockchain            │
└─────────────────────────────────────────────────────────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                    BLOCKCHAIN LAYER                              │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                    │
│  │ Block #0 │◄──│ Block #1 │◄──│ Block #2 │  ← Chain linkage  │
│  │ hash:..  │   │ hash:..  │   │ hash:..  │  (SHA-256 prev)   │
│  │ sigs:3/3 │   │ sigs:3/3 │   │ sigs:3/3 │  (ECDSA multi-sig)│
│  └──────────┘   └──────────┘   └──────────┘                    │
└─────────────────────────────────────────────────────────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                    RAFT CONSENSUS (3-10 Nodes)                   │
│                                                                  │
│     ┌──────┐          ┌──────┐          ┌──────┐               │
│     │ UGM  │◄────────►│  UI  │◄────────►│ ITB  │               │
│     │Leader│          │Follower│        │Follower│              │
│     └──────┘          └──────┘          └──────┘               │
│         ▲                ▲                ▲                      │
│         │  Log Replication (HTTP API)     │                      │
│         └────────────────┴────────────────┘                      │
└─────────────────────────────────────────────────────────────────┘
```

### ECDSA Multi-Sig (secp256k1)

Setiap blok ditandatangani oleh **≥2 dari 3 validator**:
- **UGM** — Public key disimpan di `data/`
- **UI** — Public key disimpan di `data/`
- **ITB** — Public key disimpan di `data/`

Private keys disimpan di `.keystore/` (tidak ikut git).

### Off-Chain Vault

Data mahasiswa (nama, NIM) tidak disimpan langsung di blockchain. Sistem:
1. **Hash** data → `details_hash = SHA-256(json)`
2. **Simpan** di `secured/` atau `data/offchain_vault/`
3. **Blockchain hanya menyimpan hash**

Saat verifikasi: vault di-cocokkan ulang, hash harus sama.

### Raft Consensus

| Komponen | Fungsi |
|----------|--------|
| **Leader Election** | Node otomatis pilih leader via Raft |
| **Log Replication** | Leader kirim blok ke semua follower |
| **Safety** | Hanya leader sah yang bisa commit blok |
| **Term** | Setiap election punya nomor term unik |

---

## 📁 Struktur Project

```
SecureChain-Diploma-Verifier/
├── BUILD.bat                     ← Build C++ (double-click)
├── scdv_verifier.exe             ← Binary utama (static build)
├── RUN.bat                       ← Jalankan GUI
├── install_deps.bat              ← Install Python deps
├── DEMO_3NODE.ps1                ← Demo 3 node (UGM, UI, ITB)
├── DEMO_10NODE.ps1               ← Demo 10 node cluster
│
├── src/                          ← C++ source (8 files)
│   ├── main.cpp                  ← Entry point + CLI parser
│   ├── blockchain.cpp            ← Blockchain logic + JSON persistence
│   ├── crypto_utils.cpp          ← SHA-256, AES-256-CBC (OpenSSL EVP)
│   ├── document_handler.cpp      ← File I/O
│   ├── ecdsa_utils.cpp           ← ECDSA sign/verify, ECDH, ECIES
│   ├── keystore.cpp              ← Key management (save/load/list)
│   ├── node.cpp                  ← Raft consensus + HTTP server
│   └── offchain_vault.cpp        ← Off-chain data storage
│
├── include/                      ← C++ headers (8 files)
│   ├── blockchain.h
│   ├── crypto_utils.h
│   ├── document_handler.h
│   ├── ecdsa_utils.h             ← ECDSA + ECDH + ECIES
│   ├── keystore.h
│   ├── node.h                    ← Raft consensus + HTTP API
│   ├── offchain_vault.h
│   └── nlohmann/json.hpp         ← JSON parser (vendored)
│
├── gui/                          ← Python GUI + Web
│   ├── app.py                    ← Tkinter desktop (2 tab)
│   ├── pdf_secure.py             ← Stamp + password PDF
│   ├── scdv_core.py              ← Bridge GUI → C++ exe
│   ├── vis_server.py             ← Web visualization server
│   ├── test_network.py           ← Automated network test
│   └── web/                      ← Web dashboard
│       ├── index.html            ← D3.js topology visualization
│       ├── vis.js                ← D3.js force simulation + particles
│       ├── dev.html              ← Dev dashboard (pipeline + logs)
│       ├── dev.js                ← Dashboard logic + n8n pipeline
│       ├── dev.css               ← Dashboard styling
│       └── style.css             ← Topology styling
│
├── data/                         ← Runtime data (gitignored)
│   ├── blockchain.json           ← Ledger blockchain
│   ├── node_config.json          ← Keypair + config
│   ├── node_A/pub/               ← Per-node configs (A-J)
│   └── ...
│
├── .keystore/                    ← Private keys (gitignored)
│   ├── ugm.key                   ← EC private key UGM
│   ├── ui.key                    ← EC private key UI
│   └── itb.key                   ← EC private key ITB
│
├── secured/                      ← PDF hasil securing
│   ├── manifest.json
│   └── *.pdf                     ← 127+ secured files
│
└── docs/
    ├── DEMO_STEP_BY_STEP.txt
    └── FLOWCHART_DEMO.txt
```

---

## 🎯 Skenario Lengkap

### 1. Single Node — Daftarkan Ijazah

```cmd
cd C:\laragon\www\Blockchain
BUILD.bat
scdv_verifier --keygen data
scdv_verifier register "ijazah.pdf" "UGM-010203-HUDZAIFAH" "Hudzaifah Rahman" "010203"
```

Pipeline (via web dashboard):
```
📄UPLOAD → 🔐HASH → 🔑ENCRYPT → 🗄️VAULT → ✍️MULTI-SIG → ⛓️COMMIT
   ✅         ✅         ✅          ✅          ✅            ✅
```

### 2. Single Node — Verifikasi

```cmd
scdv_verifier verify "ijazah.pdf" "UGM-010203-HUDZAIFAH"
# Output: STATUS=VERIFIED
```

### 3. Multi-Node Cluster (3 Node)

**Terminal 1:**
```cmd
scdv_verifier --keygen dataA
scdv_verifier --node dataA\node_config.json
# → LEADER, listen :8545
```

**Terminal 2:**
```cmd
scdv_verifier --keygen dataB
:: edit dataB\node_config.json → seed_peers: ["http://127.0.0.1:8545"]
scdv_verifier --node dataB\node_config.json
```

**Terminal 3:**
```cmd
scdv_verifier --keygen dataC
:: edit dataC\node_config.json → seed_peers: ["http://127.0.0.1:8545"]
scdv_verifier --node dataC\node_config.json
```

### 4. Register via HTTP API

```cmd
curl -X POST http://localhost:8545/propose ^
  -H "Content-Type: application/json" ^
  -d "{\"file_hash\":\"abc123...\",\"encrypted_label\":\"xyz...\",\"student_name\":\"Hudzaifah\",\"student_id\":\"001\"}"
```

### 5. Generate Multi-Sig Keys

```cmd
scdv_verifier --multi-keygen
# Output: Generated 3 keypairs (UGM, UI, ITB)
#         Keystore: .keystore/ugm.key, .keystore/ui.key, .keystore/itb.key
```

### 6. Verify dengan Multi-Sig

```cmd
scdv_verifier --multi-verify "ijazah.pdf" "UGM-010203-HUDZAIFAH"
# Output: Multi-sig verification: 3/3 signatures valid
```

---

## 🔌 API HTTP Reference

Base URL: `http://<node-ip>:<port>`

| Method | Endpoint | Deskripsi |
|--------|----------|-----------|
| GET | `/status` | Status node + cluster info |
| GET | `/chain` | Full blockchain JSON |
| GET | `/peers` | Daftar peer terhubung |
| GET | `/sync` | Sinkronisasi blockchain |
| GET | `/vault/<hash>` | Ambil data vault by hash |
| POST | `/propose` | Proposal blok baru |
| POST | `/vote` | Raft RequestVote RPC |
| POST | `/append` | Raft AppendEntries RPC |
| POST | `/api/blockchain/verify` | Verify dokumen via API |

Contoh:
```cmd
curl http://localhost:8545/status
curl http://localhost:8545/chain
```

---

## 🌐 Web Dashboard Akses

```cmd
python gui/vis_server.py
# Server mulai di http://localhost:9001
```

| Halaman | URL | Fungsi |
|---------|-----|--------|
| Topologi Node | `http://localhost:9001` | D3.js force graph, Raft election, particle flow |
| Dev Dashboard | `http://localhost:9001/dev.html` | n8n pipeline, live logs, blockchain explorer |

---

## 🔧 Troubleshooting

| Masalah | Solusi |
|---------|--------|
| `g++ not found` | Jalankan `BUILD.bat` (auto-detect MinGW) |
| `OpenSSL error` | Pastikan `C:\ProgramData\mingw64\mingw64\opt\` ada |
| `STATUS=NOT_FOUND` | Kode unik salah, coba `find` dulu |
| Node tidak connect | Cek IP/port, firewall, pastikan seed_peers benar |
| GUI error hitam | `pip install --user pypdf reportlab` |
| Blockchain corrupt | Hapus `data/blockchain.json`, register ulang |
| Multi-sig fail | Generate ulang: `--multi-keygen` |

---

## 🧪 Quick Test

```cmd
BUILD.bat
scdv_verifier --keygen data
scdv_verifier register "gui\app.py" "TEST-001" "Test User" "001"
scdv_verifier find "TEST-001"
scdv_verifier verify "gui\app.py" "TEST-001"
scdv_verifier validate
scdv_verifier --status
scdv_verifier --validators
python gui/test_network.py
```

---

**SecureChain v3.0 — Distributed Blockchain Document Verification**
C++17 + OpenSSL EVP + Python Tkinter + Raft Consensus + ECDSA Multi-Sig + Off-Chain Vault + D3.js Web Viz
