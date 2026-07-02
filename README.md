# SecureChain Diploma Verifier (SCDV) v2.0

**Sistem Verifikasi Ijazah Berbasis Blockchain Distributed** — Cegah Pemalsuan & Jamin Keaslian Dokumen dengan Konsensus Raft + ECDSA Cryptography

---

## 📌 Buat Apa Ini?

Mencegah pemalsuan ijazah. Setiap dokumen yang didaftarkan akan:

1. **Dihash** (SHA-256) — kalau file diubah 1 bit saja, hash beda → ketahuan
2. **Dienkripsi** dengan kode unik — hanya mahasiswa + kampus yang tahu kode-nya
3. **Ditandatangani** dengan ECDSA — bukti siapa yang mendaftarkan
4. **Dicatat di Blockchain** — data tidak bisa diubah retroaktif
5. **Direplikasi ke semua Node** — lewat Raft consensus, tidak ada single point of failure

---

## 🚀 Panduan Langkah demi Langkah (Pemula)

### Persiapan (Hanya Sekali)

**Langkah 1: Install Python dependencies**
```cmd
install_deps.bat
```
Atau manual: `python -m pip install --user pypdf reportlab`

**Langkah 2: Build program C++**
```cmd
BUILD.bat
```
Tunggu sampai muncul `BUILD SUKSES -> scdv_verifier.exe`

### Cara Pakai — 3 Mode

---

## 🅰️ Mode Sederhana (CLI Langsung)

Ini yang paling gampang. Semua di command line.

### 1. Generate Kunci (wajib sekali)
```cmd
scdv_verifier --keygen data
```
Output: `data/node_config.json` — ini kunci privat/publik node Anda.

### 2. Daftarkan Ijazah (Admin)
```cmd
scdv_verifier register "ijazah.pdf" "UGM-001-HUDZAIFAH" "Hudzaifah" "001"
```
Penjelasan argumen:
| Argumen | Contoh | Artinya |
|---------|--------|---------|
| `ijazah.pdf` | file ijazah | Path ke file PDF |
| `UGM-001-HUDZAIFAH` | kode unik | Format: KAMPUS-NIM-NAMA |
| `Hudzaifah` | nama | Nama mahasiswa |
| `001` | NIM | Nomor induk mahasiswa |

### 3. Verifikasi Ijazah (User/Perekrut)
```cmd
scdv_verifier verify "ijazah_SECURED.pdf" "UGM-001-HUDZAIFAH"
```
Kalau keluar `STATUS=VERIFIED` berarti ASLI. Kalau error berarti PALSU/DIUBAH.

### 4. Cari Data Ijazah
```cmd
scdv_verifier find "UGM-001-HUDZAIFAH"        # cari berdasarkan kode unik
scdv_verifier find_student "Hudzaifah" "001"   # cari berdasarkan nama + NIM
```

### 5. Cek Keaslian Blockchain
```cmd
scdv_verifier validate
```
Kalau `STATUS=VALID` berarti blockchain tidak pernah dirusak.

---

## 🅱️ Mode GUI (Klik-klik)

**Jalankan:**
```cmd
RUN.bat
```
Atau: `python gui/app.py`

**Tab Kampus — Daftarkan Ijazah:**
1. Pilih file PDF
2. Isi kode unik, nama, NIM
3. Klik "Daftarkan & Amankan"

**Tab Verifikasi — Cek Keaslian:**
1. Upload file `*_SECURED.pdf`
2. Masukkan kode unik
3. Klik "Verifikasi"

---

## 🅲️ Mode Jaringan (Multi-Node / Distributed)

Untuk setup beberapa komputer/node yang saling sinkron.

### Topologi
```
┌──────────┐     ┌──────────┐     ┌──────────┐
│ Node A   │◄───►│ Node B   │◄───►│ Node C   │
│ (Leader) │     │(Follower)│     │(Follower)│
└──────────┘     └──────────┘     └──────────┘
     │                │                │
     └────────────────┴────────────────┘
                   HTTP API
```

### Jalankan Node Pertama (Leader)
```cmd
scdv_verifier --node data\node_config.json
```
Node akan:
- Listen di `0.0.0.0:8545`
- Memilih dirinya jadi LEADER (karena cuma 1 node)
- Siap menerima proposal blok

### Jalankan Node Kedua (di komputer lain / terminal beda)
1. Generate keypair: `scdv_verifier --keygen data`
2. Edit `data\node_config.json` — tambahkan peer:
```json
{
  "priv_key": "...",
  "pub_key": "...",
  "listen_addr": "0.0.0.0:8545",
  "seed_peers": ["http://192.168.1.10:8545"]
}
```
3. Jalankan: `scdv_verifier --node data\node_config.json`

### Operasi di Mode Jaringan
```cmd
# Lihat status node (leader/follower, term, peers)
scdv_verifier --status

# Lihat daftar validator
scdv_verifier --validators

# Register tetap sama — otomatis proposal ke leader
scdv_verifier register "ijazah.pdf" "UGM-001-HUDZAIFAH" "Hudzaifah" "001"
```

### API HTTP (untuk integrasi aplikasi lain)

| Method | Endpoint | Fungsi |
|--------|----------|--------|
| GET | `/status` | Status node + blockchain |
| GET | `/chain` | Full blockchain |
| GET | `/peers` | Daftar peer |
| POST | `/propose` | Proposal blok baru (JSON) |
| POST | `/vote` | Raft RequestVote RPC |
| POST | `/append` | Raft AppendEntries RPC |
| GET | `/sync` | Sinkronisasi blockchain lengkap |

Contoh:
```cmd
curl http://localhost:8545/status
curl http://localhost:8545/chain
```

---

## 🔒 Cara Kerja Keamanan

### ECDSA (Elliptic Curve Digital Signature Algorithm)
Setiap node punya **keypair unik** (secp256k1 — kurva yang sama dengan Bitcoin):
- **Private key** → untuk menandatangani blok + mendekripsi data
- **Public key** → untuk verifikasi tanda tangan + enkripsi data

Setiap blok ditandatangani oleh pembuatnya. Kalau ada yang mengubah isi blok, tanda tangan jadi tidak valid.

### ECIES (Elliptic Curve Integrated Encryption Scheme)
Data sensitif dienkripsi dengan:
1. Buat kunci sementara (ephemeral key)
2. Gabung kunci sementara + public key penerima → shared secret (ECDH)
3. Shared secret → AES-256-CBC key
4. Simpan: `kunci_publik_sementara | IV | ciphertext`

Hanya pemilik private key yang bisa mendekripsi.

### Raft Consensus
Algoritma untuk menjaga semua node punya data yang sama:
1. **Leader Election** — node pilih pemimpin
2. **Log Replication** — leader kirim blok baru ke semua follower
3. **Safety** — hanya leader yang sah bisa nambah blok

---

## 📁 Struktur File Penting

```
SecureChain-Diploma-Verifier/
├── BUILD.bat                        ← Build C++ (double-click)
├── install_deps.bat                 ← Install Python deps
├── RUN.bat                          ← Jalankan GUI
├── scdv_verifier.exe                ← Program utama (hasil build)
│
├── data/
│   ├── blockchain.json              ← Ledger blockchain
│   ├── node_config.json             ← Keypair + konfigurasi node
│   └── node_<id>.pub                ← Public key (untuk share)
│
├── gui/
│   ├── app.py                       ← GUI Tkinter
│   ├── pdf_secure.py                ← Cap + password PDF
│   └── scdv_core.py                 ← Jembatan GUI → C++
│
├── src/                             ← Source code C++
└── include/                         ← Header files C++
    ├── ecdsa_utils.h                ← ECDSA + ECDH + ECIES
    ├── node.h                       ← Raft consensus + HTTP
    ├── blockchain.h                 ← Blockchain logic
    ├── crypto_utils.h               ← SHA-256 + AES
    └── document_handler.h           ← File I/O
```

---

## 🎓 Skenario Lengkap

### Di Kampus (Admin — Daftarkan Ijazah Hudzaifah)

1. Buka terminal: `cd C:\laragon\www\Blockchain`
2. Build: `BUILD.bat`
3. Generate key: `scdv_verifier --keygen data`
4. Register ijazah:
   ```
   scdv_verifier register "C:\ijazah_hudzaifah.pdf" "UGM-010203-HUDZAIFAH" "Hudzaifah Rahman" "010203"
   ```
5. Output: `STATUS=OK` — ijazah tercatat di blockchain

### Di Perusahaan (HRD — Verifikasi Ijazah Pelamar)

1. Buka terminal: `cd C:\laragon\www\Blockchain`
2. Build (kalum belum): `BUILD.bat`
3. Generate key: `scdv_verifier --keygen data`
4. Verifikasi:
   ```
   scdv_verifier verify "C:\ijazah_hudzaifah_SECURED.pdf" "UGM-010203-HUDZAIFAH"
   ```
5. Kalau `STATUS=VERIFIED` → aman. Kalau error → hati-hati!

### Dengan Jaringan 3 Node

**Terminal 1 — Node A (Leader):**
```
scdv_verifier --keygen dataA
scdv_verifier --node dataA\node_config.json
```

**Terminal 2 — Node B (Follower):**
```
scdv_verifier --keygen dataB
```
Edit `dataB\node_config.json` — tambah `"seed_peers": ["http://127.0.0.1:8545"]`
```
scdv_verifier --node dataB\node_config.json
```

**Terminal 3 — Register via API:**
```
curl -X POST http://localhost:8545/propose -H "Content-Type: application/json" -d "{\"file_hash\":\"abc...\",\"encrypted_label\":\"xyz...\",\"student_name\":\"Hudzaifah\",\"student_id\":\"001\"}"
```

---

## ❓ Troubleshooting untuk Pemula

| Masalah | Solusi |
|---------|--------|
| `'g++' not recognized` | Jalankan `BUILD.bat` (dia auto-detect g++ path) |
| `OpenSSL not found` | Pastikan folder `C:\ProgramData\mingw64\mingw64\opt\` ada |
| `STATUS=NOT_FOUND` | Kode unik salah, coba `find` dulu untuk cek |
| `STATUS=ERROR MESSAGE=...` | Baca pesan error-nya, biasanya ada petunjuk |
| Node tidak bisa connect | Cek IP + port, firewall, pastikan kedua node jalan |
| GUI hitam/error | `python -m pip install --user pypdf reportlab` |
| Blockchain corrupt/hash mismatch | Hapus `data\blockchain.json` dan register ulang |

---

## ⚠️ Sebelum Produksi

Generate kunci kuat untuk production:
```cmd
openssl rand -base64 32
```
Ganti di `gui/pdf_secure.py` variable `CAMPUS_SIGNING_KEY`.

---

## 🧪 Quick Test (Cek Semua Berfungsi)

```cmd
BUILD.bat
scdv_verifier --keygen data
scdv_verifier register "gui\app.py" "TEST-001" "Test User" "001"
scdv_verifier find "TEST-001"
scdv_verifier verify "gui\app.py" "TEST-001"
scdv_verifier validate
scdv_verifier --status
scdv_verifier --validators
```

Kalau semua `STATUS=OK/VERIFIED/VALID` — sistem siap dipakai! 🎉

---

**SecureChain v2.0 — Distributed Blockchain Document Verification**
C++17 + OpenSSL + Python Tkinter + Raft Consensus + ECDSA secp256k1
