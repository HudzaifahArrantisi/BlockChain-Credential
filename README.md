# SecureChain Diploma Verifier (SCDV)

🔐 **Sistem Verifikasi Ijazah Berbasis Blockchain** — Cegah Pemalsuan & Jamin Keaslian Dokumen

---

## 📌 Latar Belakang Masalah

### Pemalsuan Dokumen Ijazah Marak

Pemalsuan ijazah dan sertifikat pendidikan menjadi masalah serius di Indonesia:

- **Pencari kerja** menaikkan kualifikasi dengan ijazah palsu
- **Institusi pendidikan** kesulitan memverifikasi keaslian dokumen alumni
- **Perekrut/HRD** tidak punya cara pasti membedakan ijazah asli vs palsu
- **Kerugian finansial & reputasi** untuk universitas dan individu tertipu

**Contoh kasus:**
- Seorang manajer "lulus" dari universitas top ternyata ijazahnya palsu
- Alumni palsu mengakses networking eksklusif universitas
- CV palsu dideteksi setelah dipekerjakan (biaya training terbuang)

### Solusi Konvensional Tidak Efisien

- ❌ Menghubungi registrar kampus (lama, biaya admin)
- ❌ File PDF polos (mudah diubah di Photoshop)
- ❌ Tanda tangan digital (bisa di-copy atau dipalsukan)
- ❌ Database terpusat (rentan hack, single point of failure)

---

## 🎯 Apa itu SecureChain Diploma Verifier?

**SecureChain** adalah sistem **verifikasi ijazah berbasis blockchain** yang menjamin:

1. **Keaslian (Authenticity)** — Ijazah pasti berasal dari kampus resmi
2. **Integritas (Integrity)** — Dokumen tidak boleh diubah meski 1 bit
3. **Kepemilikan (Ownership)** — Hanya pemilik ijazah yang tahu kode unik (hanya diketahui alumni + kampus)
4. **Immutability** — Blockchain mencegah pembobolan data lama

### Cara Kerjanya: Double-Lock System

**Lock 1 — SHA-256 (Integritas File)**
- Kampus menghitung hash unik dari file PDF ijazah
- Saat diverifikasi: hash ulang file → cocok dengan rekaman di blockchain?
- **Jika 1 byte file berubah** (edit nama, nilai, dll) → hash beda → **GAGAL VERIFIKASI**

**Lock 2 — AES-256 (Kepemilikan Unik)**
- Kode unik (mis: `UGM-010203-HUDZAIFAH`) dienkripsi & disimpan di blockchain
- Saat diverifikasi: **hanya dengan kode unik yg benar** → lock terbuka
- **File PDF juga dikunci dengan password = kode unik** → tidak bisa dibuka tanpa kode

### Mengapa Blockchain?

- ✅ **Immutable Ledger** — Perubahan blok lama merusak chain → deteksi langsung
- ✅ **Terdesentralisasi** — Bukan satu database, tapi linked-hash chain
- ✅ **Audit Trail** — Setiap registrasi tercatat dengan timestamp
- ✅ **Open Verification** — Siapa saja bisa verifikasi tanpa izin kampus

---

## 🏗️ Arsitektur

```
GUI (Python Tkinter)     ← Tampilan user-friendly
        ↓
PDF Processing           ← Cap visual + metadata + password lock
        ↓
C++ Core (scdv_verifier.exe)
  ├─ Crypto (SHA-256, AES-256 OpenSSL)
  ├─ Blockchain (linked-hash validation)
  └─ Storage (data/blockchain.json)
```

**Keamanan dijamin oleh C++**, tidak ada kompromi:
- ✅ Enkripsi AES-256 native PDF (Adobe/Chrome standar)
- ✅ SHA-256 cryptographic hashing (standard industri)
- ✅ Linked-hash blockchain (impossible to retroactively alter)

---

## 📚 Cara Penggunaan

### Untuk Pihak Kampus (Admin) — Mendaftarkan Ijazah

**Langkah 1: Buka aplikasi**
```cmd
RUN.bat
```
Atau manual: `python gui/app.py`

**Langkah 2: Buat atau Pilih File Ijazah**
- Tab **🏛 Kampus — Daftarkan** (otomatis terbuka)
- Klik **"Buat PDF Contoh"** untuk test, atau pilih PDF ijazah asli

**Langkah 3: Isi Data Mahasiswa**
```
Kode Unik        : UGM-010203-HUDZAIFAH  (format: KAMPUS-NIM-NAMA)
Nama Mahasiswa   : Hudzaifah Rahman
NIM              : 010203
```

**Langkah 4: Klik "🔒 Daftarkan & Amankan"**

**Sistem otomatis:**
1. ✅ Beri cap visual "SECURECHAIN VERIFIED" pada PDF
2. ✅ Sisipkan metadata + tanda tangan digital (HMAC-SHA256)
3. ✅ **Kunci PDF dengan password = kode unik** (hanya bisa dibuka dengan kode)
4. ✅ Hitung SHA-256 dari file
5. ✅ Catat ke blockchain (linked-hash)
6. ✅ **Simpan ke folder `secured/`** dengan manifest.json berisi hash

**Output:**
```
✅ BERHASIL DIDAFTARKAN
─────────────────────
File aman      : /path/to/ijazah_SECURED.pdf
SHA-256        : a1b2c3d4e5f6g7h8...
Tanda tangan   : aad5a5e675f66002...
Kode pembuka   : UGM-010203-HUDZAIFAH

📁 Disimpan ke  : secured/ folder
📋 Manifest     : secured/manifest.json
```

**Langkah 5: Berikan file ke mahasiswa**
- File: `ijazah_SECURED.pdf` (file ini sudah aman & terkunci)
- Hanya bisa dibuka dengan kode unik (yang mahasiswa sudah tahu)

---

### Untuk Publik / Perekrut (User) — Memverifikasi Ijazah

**Langkah 1: Buka aplikasi** → Tab **🔍 Verifikasi**

**Langkah 2: Upload File Ijazah**
- Pilih file `*_SECURED.pdf` yang diterima dari alumni

**Langkah 3: Masukkan Kode Unik**
- Tanya alumni: "Kode unik Anda apa?"
- Contoh: `UGM-010203-HUDZAIFAH`

**Langkah 4: Klik "🔍 Verifikasi Sekarang"**

**Hasil:**

**🟢 IJAZAH ASLI — TERVERIFIKASI**
```
Nama : Hudzaifah Rahman
NIM  : 010203
Terdaftar : 2026-06-29 06:42:20

Double-Lock: Integritas SHA-256 ✓   Kode Unik ✓
File bisa dibuka dengan kode: ✓
```

**🔴 TIDAK VALID / PALSU**
```
Penyebab salah satu dari:
• File sudah diubah (hash tidak cocok)
• Kode unik salah
• Dokumen tidak terdaftar di blockchain
```

**Bonus: Cek Integritas Blockchain**
- Klik **"Cek Integritas Blockchain"** → pastikan chain belum pernah dimanipulasi

---

## 🚀 Instalasi & Setup

### Prerequisites

Sistem otomatis detect yang ada di komputer Anda:
- ✅ **g++ 15.2.0** (compiler C++)
- ✅ **OpenSSL 3.6.1** (enkripsi)
- ✅ **Python 3.10** (GUI)

### Step 1: Build C++ Core (sekali saja)

**Windows:**
```cmd
BUILD.bat
```

**Linux/WSL:**
```bash
bash build_gcc.sh
```

Output: `scdv_verifier.exe` (static, ~9MB, portabel)

### Step 2: Install Dependency GUI (sekali saja)

```cmd
install_deps.bat
```

Atau manual:
```bash
python -m pip install --user pypdf reportlab
```

### Step 3: Jalankan Aplikasi

```cmd
RUN.bat
```

Atau:
```bash
python gui/app.py
```

**GUI langsung terbuka** — siap digunakan! ✅

---

## 📁 Struktur Folder

```
SecureChain-Diploma-Verifier/
├── BUILD.bat / build_gcc.sh      ← Build C++ core
├── install_deps.bat              ← Install Python deps
├── RUN.bat                       ← Jalankan GUI
├── CARA_PAKAI.txt                ← Panduan singkat
│
├── scdv_verifier.exe             ← Executable (built)
├── gui/
│   ├── app.py                    ← GUI (Tkinter)
│   ├── pdf_secure.py             ← Cap + password lock PDF
│   └── scdv_core.py              ← Jembatan GUI ↔ C++
│
├── src/ & include/               ← C++ source code
├── data/
│   └── blockchain.json           ← Ledger (otomatis dibuat)
└── secured/                      ← Output folder (otomatis dibuat)
    ├── manifest.json             ← Hash tracking
    └── *.pdf                     ← File-file terenkripsi
```

---

## 🔒 Keamanan Berlapis

| Layer | Teknologi | Fungsi |
|-------|-----------|--------|
| **Integritas** | SHA-256 | File tidak bisa diubah 1 bit |
| **Kepemilikan** | AES-256-CBC | Hanya pemilik kode unik bisa buka |
| **Autentikasi** | HMAC-SHA256 | Metadata ditandatangani kampus |
| **Blockchain** | Linked-Hash | Chain tidak bisa di-edit retroaktif |
| **Persistence** | JSON | Data portabel, audit-able |

---

## ⚠️ Catatan Produksi

Sebelum go-live di kampus, ganti kunci rahasia:

**1. C++ Master Key** (file: `src/main.cpp`, line ~7)
```cpp
const std::string MASTER_KEY = "UBAH_DENGAN_KUNCI_KUAT_32CHAR";
```

**2. PDF Signing Key** (file: `gui/pdf_secure.py`, line ~10)
```python
CAMPUS_SIGNING_KEY = b"UBAH_DENGAN_KUNCI_KUAT_32CHAR"
```

Generate kunci kuat:
```bash
openssl rand -base64 32
```

---

## 🎓 Contoh End-to-End

### Skenario: Alumni Hudzaifah Ingin Verifikasi Ijazahnya

**Minggu 1 (Admin UGM):**
1. Admin buka aplikasi → Tab "Kampus"
2. Upload ijazah Hudzaifah (PDF polos dari universitas database)
3. Isi: NIM `010203`, Nama `Hudzaifah Rahman`, Kode `UGM-010203-HUDZAIFAH`
4. Klik "Daftarkan & Amankan"
5. **Sistem auto:** cap + tanda tangan + kunci PDF + catat blockchain
6. File `ijazah_SECURED.pdf` ready → kirim ke Hudzaifah via email

**Minggu 2 (Hudzaifah Apply Kerja):**
1. Hudzaifah lampirkan file `ijazah_SECURED.pdf` ke aplikasi kerja
2. HRD terima file + tanya kode: "Apa kode unik ijazah Anda?"
3. Hudzaifah beri kode: `UGM-010203-HUDZAIFAH`
4. HRD buka aplikasi → Tab "Verifikasi"
5. Upload file + masuk kode
6. **Klik Verifikasi** → **🟢 IJAZAH ASLI TERVERIFIKASI**
7. HRD confident → Hudzaifah lolos tahap verifikasi

---

## 🛠️ Troubleshooting

**"OpenSSL not found"**
→ Download dari https://slproweb.com/products/Win32OpenSSL.html (Win64 Light)

**"MSVC compiler not found"**
→ Install Visual Studio Community 2022 dengan C++ development tools

**"GUI tidak mau jalan"**
→ Pastikan `scdv_verifier.exe` sudah built: `BUILD.bat` dulu

---

## 📖 Dokumentasi Lengkap

- `CARA_PAKAI.txt` — Panduan singkat (5 menit)
- `AGENTS.md` — Technical architecture (untuk developer)
- `BUILD_INSTRUCTIONS.md` — Build details per platform (untuk developer)

---

**Dibuat dengan ❤️ menggunakan C++17 + Python + Blockchain**
