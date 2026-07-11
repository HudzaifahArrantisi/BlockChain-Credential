# SecureChain Diploma Verifier (SCDV) v3.0

**Sistem Verifikasi Ijazah Berbasis Blockchain Terdistribusi**

> Cegah Pemalsuan Dokumen Akademik dengan Teknologi Blockchain, Kriptografi Modern (SHA-256, AES-256, ECDSA), dan Konsensus Raft

---

## 📚 Daftar Isi

1. [Latar Belakang](#-latar-belakang)
2. [Pendahuluan](#-pendahuluan)
3. [Apa Itu SecureChain Diploma Verifier?](#-apa-itu-securechain-diploma-verifier)
4. [Konsep Dasar untuk Pemula](#-konsep-dasar-untuk-pemula)
5. [Arsitektur Sistem](#-arsitektur-sistem)
6. [Alur Kerja Sistem](#-alur-kerja-sistem)
7. [Flowchart Teknis Lengkap](#-flowchart-teknis-lengkap)
8. [Struktur Folder Proyek](#-struktur-folder-proyek)
9. [Quick Start (Cara Menjalankan)](#-quick-start-cara-menjalankan)
10. [Skenario Penggunaan](#-skenario-penggunaan)
11. [API HTTP Reference](#-api-http-reference)
12. [Web Dashboard](#-web-dashboard)
13. [Model Keamanan (Security Model)](#-model-keamanan-security-model)
14. [Troubleshooting](#-troubleshooting)
15. [Quick Test](#-quick-test)

---

## 🧭 Latar Belakang

### Masalah: Pemalsuan Ijazah di Indonesia

Pemalsuan ijazah merupakan persoalan serius yang sudah lama terjadi di Indonesia dan di seluruh dunia. Berikut beberapa fakta yang melatarbelakangi proyek ini:

- **Maraknya jual-beli ijazah palsu.** Oknum-oknum menawarkan ijazah palsu dari berbagai perguruan tinggi, baik secara online maupun offline. Kasus ini sudah berulang kali diungkap oleh kepolisian, namun terus bermunculan karena mekanisme verifikasi yang ada masih lemah.

- **Proses verifikasi manual yang lambat dan rentan.** Saat ini, ketika sebuah perusahaan atau instansi ingin mengecek keaslian ijazah, mereka harus menghubungi pihak kampus secara manual — melalui telepon, surat, atau email. Proses ini bisa memakan waktu berminggu-minggu, dan hasilnya pun tidak selalu bisa dipercaya karena bergantung pada kelengkapan arsip fisik kampus.

- **Arsip kampus bisa rusak atau hilang.** Bencana alam, kebakaran, atau kelalaian administrasi bisa menyebabkan data alumni hilang. Ketika arsip rusak, tidak ada cara untuk membuktikan keaslian dokumen lama.

- **Dokumen digital mudah diedit.** File PDF bisa dibuka dan dimodifikasi dengan mudah menggunakan software editing. Tanpa mekanisme pengamanan digital, tidak ada cara untuk membuktikan apakah sebuah file PDF masih asli atau sudah diubah.

- **Belum ada standar verifikasi digital terpusat.** Indonesia belum memiliki sistem nasional yang memungkinkan siapa saja memverifikasi keaslian ijazah secara instan dan terpercaya.

### Mengapa Solusi Konvensional Tidak Cukup?

| Metode Lama | Kelemahan |
|-------------|-----------|
| Stempel & tanda tangan basah | Bisa dipalsukan, tidak bisa dicek digital |
| Hologram / kertas khusus | Mahal, bisa ditiru, hanya perlindungan fisik |
| Database kampus online | Terpusat (single point of failure), bisa di-hack |
| QR code biasa | Hanya berisi link, datanya bisa diganti oleh admin |

### Solusi: Blockchain

**Blockchain** menawarkan solusi fundamental karena sifat-sifatnya yang unik:

- **Immutable (Tidak bisa diubah)** — Sekali data dicatat di blockchain, tidak ada seorang pun yang bisa mengubah atau menghapusnya tanpa merusak seluruh rantai.
- **Terdesentralisasi** — Data tidak disimpan di satu server milik satu pihak. Beberapa node (misalnya UGM, UI, ITB) masing-masing menyimpan salinan lengkap, sehingga tidak ada single point of failure.
- **Transparan & Dapat Diaudit** — Siapa saja bisa memverifikasi keaslian data tanpa harus mempercayai satu pihak tertentu.
- **Cryptographically Secured** — Menggunakan algoritma kriptografi tingkat militer (SHA-256, AES-256, ECDSA) yang secara matematis sangat sulit dipalsukan.

---

## 📖 Pendahuluan

### Tentang Proyek Ini

**SecureChain Diploma Verifier (SCDV)** adalah proyek open-source yang membangun sistem verifikasi ijazah digital menggunakan teknologi blockchain. Proyek ini dikembangkan sebagai bukti konsep (*proof of concept*) bagaimana blockchain bisa diterapkan di dunia nyata untuk melindungi keaslian dokumen akademik.

### Tujuan Utama

1. **Membuat ijazah tidak bisa dipalsukan** — Setiap ijazah yang didaftarkan akan memiliki "sidik jari digital" (hash) unik yang dicatat secara permanen di blockchain.

2. **Memungkinkan verifikasi instan** — Siapa saja (perusahaan, instansi, mahasiswa) bisa mengecek keaslian ijazah dalam hitungan detik, tanpa perlu menghubungi kampus.

3. **Menerapkan keamanan berlapis** — Tidak hanya satu, tapi enam lapisan keamanan diterapkan agar sistem benar-benar tahan terhadap pemalsuan.

4. **Membangun infrastruktur terdistribusi** — Beberapa kampus (node) bersama-sama menjaga integritas data melalui konsensus Raft, sehingga tidak ada satu pihak pun yang bisa memanipulasi data sendirian.

### Siapa yang Menggunakan?

| Pengguna | Peran |
|----------|-------|
| **Pihak Kampus** (Admin) | Mendaftarkan ijazah ke blockchain, membuat PDF yang diamankan |
| **Mahasiswa / Alumni** | Menerima ijazah digital yang bisa diverifikasi kapan saja |
| **Perusahaan / Instansi** | Memverifikasi keaslian ijazah pelamar secara mandiri dan instan |
| **Auditor / Publik** | Memeriksa integritas seluruh blockchain |

### Teknologi yang Digunakan

| Komponen | Teknologi | Penjelasan Singkat |
|----------|-----------|-------------------|
| **Inti Blockchain & Kriptografi** | C++17 + OpenSSL | Bahasa pemrograman berkinerja tinggi untuk operasi kriptografi |
| **Hashing** | SHA-256 | Algoritma hash yang menghasilkan "sidik jari" unik 64 karakter dari sebuah file |
| **Enkripsi Simetris** | AES-256-CBC | Algoritma enkripsi kelas militer untuk mengunci data |
| **Tanda Tangan Digital** | ECDSA secp256k1 | Kurva eliptik untuk memastikan blok benar-benar berasal dari node yang sah |
| **Konsensus** | Raft | Protokol agar beberapa node bisa sepakat siapa pemimpin dan data mana yang valid |
| **Penyimpanan Off-Chain** | Off-Chain Vault | Data pribadi (nama, NIM) disimpan terpisah, hanya hash-nya yang masuk blockchain |
| **GUI Desktop** | Python Tkinter | Antarmuka grafis untuk pengguna non-teknis |
| **Pengamanan PDF** | pypdf + reportlab + qrcode | Cap visual, QR code, watermark, tanda tangan HMAC, enkripsi AES pada PDF |
| **Web Dashboard** | HTML + D3.js + SSE | Visualisasi topologi jaringan node secara real-time |
| **Data Format** | JSON (nlohmann/json) | Format data ringan untuk blockchain ledger dan konfigurasi |

### Bagaimana Proyek Ini Berbeda dari Blockchain Lain?

Berbeda dengan cryptocurrency (Bitcoin, Ethereum), proyek ini:

- **Tidak ada mata uang/token** — Murni untuk verifikasi dokumen
- **Konsorsium tertutup** — Hanya node kampus yang terdaftar bisa berpartisipasi (bukan publik seperti Bitcoin)
- **Konsensus Raft** — Lebih cepat dan efisien dari Proof-of-Work (tidak perlu "mining")
- **Fokus pada dokumen** — Dioptimalkan khusus untuk menyimpan hash dokumen, bukan transaksi keuangan

---

## 🎯 Apa Itu SecureChain Diploma Verifier?

Secara sederhana, SCDV bekerja seperti **"notaris digital"** untuk ijazah:

```
📄 Ijazah PDF asli
      ↓
🔐 Dicap + Dikunci + Ditandatangani secara digital
      ↓
⛓️ Hash-nya dicatat permanen di blockchain
      ↓
✅ Siapa saja bisa verifikasi: ASLI atau PALSU
```

Setiap dokumen yang didaftarkan melewati **6 tahap pengamanan**:

| # | Tahap | Apa yang Terjadi | Analogi Dunia Nyata |
|---|-------|------------------|---------------------|
| 1 | 📄 Upload | File PDF diterima sistem | Menyerahkan berkas ke loket |
| 2 | 🔐 SHA-256 Hash | File di-hash → "sidik jari" digital 64 karakter | Mengambil sidik jari dokumen |
| 3 | 🔑 AES-256 Encrypt | Kode unik dienkripsi, hanya bisa dibuka dengan kunci | Mengunci berkas dalam brankas |
| 4 | 🗄️ Off-Chain Vault | Data pribadi disimpan terpisah (hanya hash di chain) | Menyimpan data sensitif di ruang khusus |
| 5 | ✍️ ECDSA Multi-Sig | Blok ditandatangani ≥2 dari 3 validator | Tanda tangan 2 dari 3 saksi |
| 6 | ⛓️ Block Commit | Blok dicatat di blockchain via Raft consensus | Dicatat di buku besar yang disaksikan semua pihak |

---

## 🧩 Konsep Dasar untuk Pemula

Kalau kamu baru pertama kali mendengar istilah-istilah ini, berikut penjelasan sederhana:

### Apa Itu Blockchain?

Bayangkan sebuah **buku besar** (ledger) yang:
- Dimiliki bersama oleh beberapa pihak (node)
- Setiap halaman (block) berisi catatan dan "cap" dari halaman sebelumnya
- Kalau seseorang mencoba mengubah satu halaman, semua halaman setelahnya menjadi tidak cocok → ketahuan

```
Block 0          Block 1          Block 2
┌──────────┐    ┌──────────┐    ┌──────────┐
│ Genesis  │◄───│ prev: #0 │◄───│ prev: #1 │
│ hash: a1 │    │ data: .. │    │ data: .. │
└──────────┘    │ hash: b2 │    │ hash: c3 │
                └──────────┘    └──────────┘
```

Kalau data di Block 1 diubah → hash Block 1 berubah → `prev` di Block 2 tidak cocok lagi → **KETAHUAN!**

### Apa Itu Hash (SHA-256)?

**Hash** itu seperti sidik jari digital. Fungsi SHA-256 mengambil data apapun (file, teks) dan menghasilkan string 64 karakter yang:
- **Unik** — Input berbeda → hash berbeda (hampir mustahil ada 2 input yang menghasilkan hash sama)
- **Satu arah** — Dari hash, mustahil mendapatkan kembali data aslinya
- **Sensitif** — Ubah 1 huruf saja → hash berubah total

```
"Ijazah_Hudzaifah.pdf"  → hash: a3f7c2e8b1d4...   (64 karakter)
"Ijazah_Hudzaifah.pdf" (diubah 1 byte) → hash: 9e1b7f3a5c8d...   (BERBEDA TOTAL!)
```

### Apa Itu Enkripsi (AES-256)?

**Enkripsi** = mengacak data agar tidak bisa dibaca tanpa kunci.

```
Data asli:  "UGM-010203-HUDZAIFAH"
            ↓ Enkripsi (kunci rahasia)
Terenkripsi: "x7f8g9h0i1j2k3l4m5..."
            ↓ Dekripsi (kunci yang sama)
Data asli:  "UGM-010203-HUDZAIFAH"
```

AES-256 artinya kunci sepanjang 256 bit — ada 2²⁵⁶ kemungkinan kombinasi kunci. Untuk membongkar secara brute force, butuh waktu lebih lama dari usia alam semesta.

### Apa Itu ECDSA?

**ECDSA** (Elliptic Curve Digital Signature Algorithm) = tanda tangan digital berbasis matematika kurva eliptik. Mirip tanda tangan manusia, tapi:
- Mustahil dipalsukan (berdasarkan problem matematika yang sangat sulit)
- Bisa diverifikasi siapa saja menggunakan kunci publik
- Digunakan Bitcoin dan banyak sistem keamanan modern

### Apa Itu Raft Consensus?

**Raft** = protokol untuk membuat beberapa komputer (node) sepakat. Bayangkan:

1. Ada 3 node: **UGM**, **UI**, **ITB**
2. Salah satu terpilih sebagai **Leader** (misalnya UGM)
3. Ketika ada data baru, Leader mengirimkan ke semua Follower
4. Jika mayoritas (≥2 dari 3) setuju → data dicatat
5. Kalau Leader mati, Follower otomatis memilih Leader baru

Ini membuat sistem tetap berjalan meskipun ada node yang down.

---

## 🏗️ Arsitektur Sistem

Proyek ini adalah **aplikasi hybrid** yang menggabungkan dua bahasa pemrograman:

```
┌─────────────────────────────────────────────────────────────┐
│                    PENGGUNA (USER)                           │
├──────────────┬──────────────────┬───────────────────────────┤
│  GUI Tkinter │  Web Dashboard   │  CLI (Command Line)       │
│  (app.py)    │  (vis_server.py) │  (scdv_verifier.exe)      │
│  Desktop     │  Browser         │  Terminal                 │
├──────────────┴──────────────────┴───────────────────────────┤
│              PYTHON BRIDGE (scdv_core.py)                   │
│   Memanggil exe C++ via subprocess, parsing KEY=VALUE       │
├─────────────────────────────────────────────────────────────┤
│              INTI C++ (scdv_verifier.exe)                    │
│   blockchain.cpp │ crypto_utils.cpp │ document_handler.cpp  │
│   ecdsa_utils.cpp │ node.cpp │ keystore.cpp │ offchain_vault│
├─────────────────────────────────────────────────────────────┤
│              OPENSSL (Kriptografi)                           │
│   SHA-256 │ AES-256-CBC │ ECDSA secp256k1                  │
├─────────────────────────────────────────────────────────────┤
│              DATA LAYER                                     │
│   blockchain.json │ .keystore/ │ secured/ │ data/uploads/   │
└─────────────────────────────────────────────────────────────┘
```

### Komponen Utama

| File | Bahasa | Fungsi |
|------|--------|--------|
| `src/main.cpp` | C++ | Program utama: menu interaktif & mode argv (`register`/`verify`/`validate`/`find`) |
| `src/blockchain.cpp` | C++ | Logika rantai blok: tambah blok, validasi chain, simpan/muat JSON |
| `src/crypto_utils.cpp` | C++ | SHA-256 hashing & AES-256-CBC enkripsi/dekripsi via OpenSSL |
| `src/document_handler.cpp` | C++ | Baca file & hitung hash konten |
| `src/ecdsa_utils.cpp` | C++ | Generate & verifikasi tanda tangan digital ECDSA secp256k1 |
| `src/keystore.cpp` | C++ | Simpan & muat keypair ECDSA ke disk |
| `src/node.cpp` | C++ | HTTP server & Raft consensus (port 8545-8559) |
| `src/offchain_vault.cpp` | C++ | Penyimpanan data sensitif di luar blockchain (terenkripsi) |
| `gui/app.py` | Python | GUI desktop Tkinter: tab Daftarkan & tab Verifikasi |
| `gui/pdf_secure.py` | Python | Cap visual (badge merah + QR + watermark), HMAC-SHA256, enkripsi PDF |
| `gui/scdv_core.py` | Python | Jembatan GUI→exe: panggil scdv_verifier.exe, parsing output |
| `gui/vis_server.py` | Python | Web dashboard server (HTTP + SSE), API register |
| `gui/web/index.html` | HTML/JS | Dashboard: grafik topologi D3.js + form register + log blockchain |
| `gui/web/vis.js` | JS | Rendering grafik force-directed D3.js dengan animasi kabel |

---

## 🔄 Alur Kerja Sistem

### A. Pendaftaran Ijazah (Registration)

```
Kampus Admin                   Sistem SCDV                    Blockchain
    │                              │                              │
    │  1. Upload PDF + Kode Unik   │                              │
    │─────────────────────────────►│                              │
    │                              │  2. Baca file                │
    │                              │  3. Cap visual (badge+QR)    │
    │                              │  4. Tanda tangan HMAC        │
    │                              │  5. Kunci PDF (AES-256)      │
    │                              │  6. Hitung SHA-256 hash      │
    │                              │  7. Enkripsi label (AES-256) │
    │                              │  8. Simpan vault off-chain   │
    │                              │                              │
    │                              │  9. Commit ke blockchain     │
    │                              │─────────────────────────────►│
    │                              │                              │
    │  10. Terima PDF Secured      │                              │
    │◄─────────────────────────────│                              │
```

### B. Verifikasi Ijazah (Verification)

```
Perusahaan/Publik              Sistem SCDV                    Blockchain
    │                              │                              │
    │  1. Upload PDF + Kode Unik   │                              │
    │─────────────────────────────►│                              │
    │                              │  2. Hitung SHA-256 hash      │
    │                              │  3. Cari hash di blockchain  │
    │                              │──────────────────────────────►│
    │                              │  4. Ditemukan? Cocok?        │
    │                              │◄──────────────────────────────│
    │                              │  5. Dekripsi label (AES-256) │
    │                              │  6. Bandingkan kode unik     │
    │                              │                              │
    │  7. Hasil: ASLI / PALSU      │                              │
    │◄─────────────────────────────│                              │
```

---

## 📊 Flowchart Teknis Lengkap

### Fase 1: Pendaftaran (Registration)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     TAHAP 1: USER UPLOAD FILE PDF ASLI                  │
└─────────────────────────────────────────────────────────────────────────┘
                                    ▼
                    ┌───────────────────────────────┐
                    │  📄 User Upload via GUI/CLI   │
                    │  File: ijazah_asli.pdf        │
                    │  Kode: UGM-010203-HUDZAIFAH   │
                    │  Nama: Hudzaifah Rahman       │
                    │  NIM: 010203                  │
                    └───────────────────┬───────────┘
                                        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│               TAHAP 2: SECURITY LAYER 1 — PDF SECURING                  │
├─────────────────────────────────────────────────────────────────────────┤
│  📌 Komponen: gui/pdf_secure.py                                         │
│  Proses:                                                                │
│    a. Cap visual: badge merah "SECURECHAIN VERIFIED" + QR code          │
│    b. Watermark diagonal pada halaman                                   │
│    c. HMAC-SHA256 signature di metadata PDF                             │
│    d. Enkripsi PDF dengan password = Kode Unik (AES-256)               │
│  Output: file_SECURED.pdf (terkunci, hanya bisa dibuka dengan kode)    │
└────────────────────────────────┬────────────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│               TAHAP 3: SECURITY LAYER 2 — HASHING                       │
├─────────────────────────────────────────────────────────────────────────┤
│  📌 Komponen: document_handler.cpp + crypto_utils.cpp                   │
│  Proses: SHA-256(isi file PDF yang sudah di-secure)                     │
│  Output: file_hash = "a1b2c3d4e5f6..."  (64 karakter hex)              │
│  Fungsi: Sidik jari unik — ubah 1 bit saja → hash berubah total       │
└────────────────────────────────┬────────────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│               TAHAP 4: SECURITY LAYER 3 — LABEL ENCRYPTION              │
├─────────────────────────────────────────────────────────────────────────┤
│  📌 Komponen: crypto_utils.cpp (OpenSSL EVP AES-256-CBC)                │
│  Input: label = "UGM-010203-HUDZAIFAH"                                  │
│  Key: MASTER_KEY (kunci rahasia di main.cpp)                            │
│  Output: encrypted_label = "x7f8g9h0i1j2..."                           │
│  Fungsi: Hanya pemilik kunci yang bisa membuktikan kepemilikan         │
└────────────────────────────────┬────────────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│               TAHAP 5: SECURITY LAYER 4 — OFF-CHAIN VAULT               │
├─────────────────────────────────────────────────────────────────────────┤
│  📌 Komponen: offchain_vault.cpp                                        │
│  Data sensitif (nama, NIM) disimpan TERPISAH dari blockchain            │
│  Yang masuk blockchain hanya: file_hash + encrypted_label               │
│  Fungsi: Privasi — data pribadi tidak tersebar di semua node           │
└────────────────────────────────┬────────────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│               TAHAP 6: SECURITY LAYER 5 & 6 — SIGN & COMMIT            │
├─────────────────────────────────────────────────────────────────────────┤
│  📌 Komponen: ecdsa_utils.cpp + blockchain.cpp + node.cpp               │
│  a. Blok ditandatangani ECDSA oleh node validator                       │
│  b. Multi-sig: minimal 2 dari 3 node harus setuju                      │
│  c. Raft consensus: Leader merelikasi blok ke Follower                  │
│  d. Jika mayoritas setuju → blok di-commit ke blockchain.json           │
│  Status: ⛓️ Dicatat permanen, tidak bisa diubah                        │
└─────────────────────────────────────────────────────────────────────────┘
```

### Fase 2: Verifikasi

```
┌─────────────────────────────────────────────────────────────────────────┐
│                  USER UPLOAD PDF SECURED + KODE UNIK                    │
└────────────────────────────────┬────────────────────────────────────────┘
                                  ▼
                    ┌─────────────────────────┐
                    │  SHA-256(file_secured)   │
                    │  → hitung hash file     │
                    └────────────┬────────────┘
                                  ▼
                    ┌─────────────────────────┐
                    │  Cari hash di blockchain│    Hash tidak ditemukan?
                    │  (blockchain.json)      │───► ❌ TIDAK TERDAFTAR
                    └────────────┬────────────┘
                                  ▼ (ditemukan)
                    ┌─────────────────────────┐
                    │  Dekripsi label di blok  │    Kode tidak cocok?
                    │  Bandingkan dengan input │───► ❌ KODE SALAH
                    └────────────┬────────────┘
                                  ▼ (cocok)
                    ┌─────────────────────────┐
                    │  ✅ STATUS = VERIFIED    │
                    │  Ijazah ASLI & UTUH     │
                    │  + info: Nama, NIM, dst │
                    └─────────────────────────┘
```

---

## 📁 Struktur Folder Proyek

```
C:\laragon\www\Blockchain\
│
├── src/                          ← Source code C++ (inti sistem)
│   ├── main.cpp                  ← Program utama (CLI interaktif + argv)
│   ├── blockchain.cpp            ← Logika blockchain + JSON persistence
│   ├── crypto_utils.cpp          ← SHA-256 + AES-256-CBC (OpenSSL)
│   ├── document_handler.cpp      ← Baca file + hitung hash
│   ├── ecdsa_utils.cpp           ← ECDSA secp256k1 key & sign
│   ├── keystore.cpp              ← Simpan/muat keypair
│   ├── node.cpp                  ← HTTP server + Raft consensus
│   └── offchain_vault.cpp        ← Vault data sensitif off-chain
│
├── include/                      ← Header C++ (1:1 dengan src/)
│   ├── blockchain.h
│   ├── crypto_utils.h
│   ├── document_handler.h
│   ├── ecdsa_utils.h
│   ├── keystore.h
│   ├── node.h
│   ├── offchain_vault.h
│   └── nlohmann/json.hpp         ← Library JSON (vendored)
│
├── gui/                          ← Antarmuka pengguna (Python)
│   ├── app.py                    ← GUI desktop Tkinter
│   ├── pdf_secure.py             ← Cap + kunci + tanda tangan PDF
│   ├── scdv_core.py              ← Jembatan GUI → exe C++
│   ├── vis_server.py             ← Web dashboard server (HTTP + SSE)
│   └── web/                      ← Web dashboard frontend
│       ├── index.html            ← Dashboard: topologi D3.js + form register
│       └── vis.js                ← D3.js force graph + animasi kabel
│
├── data/                         ← Data runtime (gitignored)
│   ├── blockchain.json           ← Ledger blockchain
│   └── uploads/                  ← File upload + PDF secured
│
├── .keystore/                    ← Kunci privat ECDSA (gitignored)
│   ├── ugm.key
│   ├── ui.key
│   └── itb.key
│
├── secured/                      ← PDF hasil securing + manifest.json
│
├── scdv_verifier.exe             ← Executable hasil build C++
├── BUILD.bat                     ← Build script Windows
├── build_gcc.sh                  ← Build script Linux/Git Bash
├── RUN.bat                       ← Jalankan GUI
├── install_deps.bat              ← Install dependency Python
├── CMakeLists.txt                ← CMake (khusus Linux/WSL)
│
└── docs/                         ← Dokumentasi tambahan
```

---

## 🚀 Quick Start (Cara Menjalankan)

### Prasyarat

- **Windows 10/11** (atau Linux/WSL)
- **MinGW g++** (C++17) — biasanya sudah ada jika pakai Laragon/MSYS2
- **OpenSSL dev libs** di `C:\ProgramData\mingw64\mingw64\opt\`
- **Python 3.8+** dengan pip

### Langkah 1 — Build Inti C++ (Sekali Saja)

```cmd
BUILD.bat
```
Atau di Git Bash:
```bash
bash build_gcc.sh
```
Hasil: `scdv_verifier.exe` (static build, portabel tanpa DLL tambahan).

### Langkah 2 — Install Dependency GUI (Sekali Saja)

```cmd
install_deps.bat
```
Atau manual:
```cmd
pip install --user pypdf reportlab qrcode[pil] Pillow
```

### Langkah 3 — Jalankan Aplikasi

**GUI Desktop:**
```cmd
RUN.bat
:: atau
python gui\app.py
```

**Web Dashboard:**
```cmd
python gui\vis_server.py
:: Buka browser → http://localhost:8080
```

**CLI (Command Line):**
```cmd
scdv_verifier register "ijazah.pdf" "UGM-010203-HUDZAIFAH" "Hudzaifah" "010203"
scdv_verifier verify "ijazah.pdf" "UGM-010203-HUDZAIFAH"
scdv_verifier validate
```

---

## 🎯 Skenario Penggunaan

### 1. Mendaftarkan Ijazah (Mode GUI)

1. Buka aplikasi (`RUN.bat`)
2. Pilih tab **"Kampus — Daftarkan"**
3. Pilih file PDF ijazah
4. Isi: Kode Unik, Nama Mahasiswa, NIM
5. Klik **"Daftarkan & Amankan"**
6. Hasil: file `*_SECURED.pdf` — berikan ke mahasiswa

### 2. Memverifikasi Ijazah (Mode GUI)

1. Pilih tab **"Verifikasi"**
2. Pilih file `*_SECURED.pdf`
3. Masukkan Kode Unik
4. Klik **"Verifikasi Sekarang"**
5. Hasil: ✅ HIJAU = ASLI | ❌ MERAH = PALSU

### 3. Mode CLI

```cmd
:: Generate keypair
scdv_verifier --keygen data

:: Register
scdv_verifier register "ijazah.pdf" "UGM-010203-HUDZAIFAH" "Hudzaifah Rahman" "010203"

:: Verify
scdv_verifier verify "ijazah.pdf" "UGM-010203-HUDZAIFAH"
:: Output: STATUS=VERIFIED → ASLI

:: Cari blok berdasarkan kode
scdv_verifier find "UGM-010203-HUDZAIFAH"

:: Validasi integritas seluruh blockchain
scdv_verifier validate
```

### 4. Multi-Node Cluster (3 Node)

**Terminal 1 (Leader):**
```cmd
scdv_verifier --keygen dataA
scdv_verifier --node dataA\node_config.json
```

**Terminal 2 (Follower 1):**
```cmd
scdv_verifier --keygen dataB
:: Edit dataB\node_config.json → seed_peers: ["http://127.0.0.1:8545"]
scdv_verifier --node dataB\node_config.json
```

**Terminal 3 (Follower 2):**
```cmd
scdv_verifier --keygen dataC
:: Edit dataC\node_config.json → seed_peers: ["http://127.0.0.1:8545"]
scdv_verifier --node dataC\node_config.json
```

### 5. Register via HTTP API

```cmd
curl -X POST http://localhost:8545/propose ^
  -H "Content-Type: application/json" ^
  -d "{\"file_hash\":\"abc123...\",\"encrypted_label\":\"xyz...\",\"student_name\":\"Hudzaifah\",\"student_id\":\"001\"}"
```

---

## 🔌 API HTTP Reference

Base URL: `http://<node-ip>:<port>` (default: `http://localhost:8545`)

| Method | Endpoint | Deskripsi |
|--------|----------|-----------|
| `GET` | `/status` | Status node + info cluster |
| `GET` | `/chain` | Seluruh blockchain dalam format JSON |
| `GET` | `/peers` | Daftar peer yang terhubung |
| `GET` | `/sync` | Sinkronisasi blockchain antar node |
| `GET` | `/vault/<hash>` | Ambil data vault berdasarkan hash |
| `POST` | `/propose` | Ajukan blok baru ke jaringan |
| `POST` | `/vote` | Raft RequestVote RPC |
| `POST` | `/append` | Raft AppendEntries RPC |
| `POST` | `/api/blockchain/verify` | Verifikasi dokumen via API |

---

## 🌐 Web Dashboard

```cmd
python gui/vis_server.py
:: Server berjalan di http://localhost:8080
```

Fitur dashboard:
- **Topologi Node** — Grafik D3.js force-directed yang menampilkan semua node, siapa Leader, siapa Follower, kabel animasi antar node
- **Register Diploma** — Form untuk mendaftarkan ijazah langsung dari browser
- **Blockchain Log** — Riwayat blok yang sudah tercatat

---

## 🛡️ Model Keamanan (Security Model)

SCDV menerapkan **pertahanan berlapis** (*defense in depth*):

| Layer | Mekanisme | Apa yang Dilindungi |
|-------|-----------|---------------------|
| **1. Integritas File** | SHA-256 hash | Mendeteksi perubahan sekecil apapun pada file |
| **2. Kepemilikan** | AES-256 encrypted label | Membuktikan siapa pemilik sah dokumen |
| **3. Cap Visual** | Badge merah + QR code + watermark | Bukti visual bahwa dokumen sudah terverifikasi |
| **4. Tanda Tangan Digital** | HMAC-SHA256 di metadata PDF | Memastikan cap tidak dipalsukan |
| **5. Proteksi PDF** | AES-256 password lock | PDF hanya bisa dibuka dengan Kode Unik |
| **6. Immutability** | Blockchain linked-hash | Data yang sudah dicatat tidak bisa diubah |
| **7. Desentralisasi** | Raft consensus (multi-node) | Tidak ada single point of failure |
| **8. Tanda Tangan Node** | ECDSA secp256k1 multi-sig | Memastikan blok berasal dari node yang sah |
| **9. Privasi** | Off-chain vault | Data pribadi tidak tersebar ke semua node |

### Ilustrasi "Double-Lock"

```
Lock 1 (Integritas):  SHA-256(file) harus cocok dengan hash di blockchain
                      → Ubah 1 byte pada file → verifikasi GAGAL

Lock 2 (Kepemilikan): Kode unik dienkripsi AES-256 di dalam blok
                      → Tanpa kode yang benar → verifikasi GAGAL

Bonus Lock (PDF):     File PDF terkunci, tidak bisa dibuka tanpa kode
                      → Bahkan membuka file saja butuh kode yang benar
```

---

## 🔧 Troubleshooting

| Masalah | Solusi |
|---------|--------|
| `g++ not found` | Jalankan `BUILD.bat` yang auto-detect MinGW, atau install MinGW manual |
| `OpenSSL error` | Pastikan `C:\ProgramData\mingw64\mingw64\opt\` ada dan berisi OpenSSL dev |
| `STATUS=NOT_FOUND` | Kode unik salah — coba `scdv_verifier find "KODE"` dulu |
| Node tidak connect | Cek IP/port, firewall, pastikan `seed_peers` benar di `node_config.json` |
| GUI error / layar hitam | Jalankan `pip install --user pypdf reportlab qrcode[pil] Pillow` |
| Blockchain corrupt | Hapus `data/blockchain.json`, register ulang dari awal |
| Multi-sig fail | Generate ulang keypair: `scdv_verifier --multi-keygen` |
| DLL error / segfault | Pastikan build static (`BUILD.bat`), jangan pakai CMake di Windows |

---

## 🧪 Quick Test

```cmd
:: Build
BUILD.bat

:: Generate keypair
scdv_verifier --keygen data

:: Test register
scdv_verifier register "gui\app.py" "TEST-001" "Test User" "001"

:: Test find
scdv_verifier find "TEST-001"

:: Test verify
scdv_verifier verify "gui\app.py" "TEST-001"

:: Test blockchain integrity
scdv_verifier validate

:: Test node status
scdv_verifier --status
scdv_verifier --validators
```

---

## ⚠️ Catatan Keamanan Produksi

Sebelum deploy ke lingkungan produksi, **WAJIB** ganti kunci rahasia default:

1. **MASTER_KEY** di `src/main.cpp` — kunci AES untuk enkripsi label
2. **CAMPUS_SIGNING_KEY** di `gui/pdf_secure.py` — kunci HMAC untuk tanda tangan PDF

Generate kunci kuat:
```bash
openssl rand -base64 32
```

---

**SecureChain Diploma Verifier v3.0**
C++17 · OpenSSL · Python Tkinter · Raft Consensus · ECDSA Multi-Sig · Off-Chain Vault · D3.js Web Dashboard
