# Prompt: Verifikasi Berbasis Kode Unik (Tanpa Upload File)

## Tujuan

Ubah **Tab Verifikasi** sehingga pengguna **tidak perlu upload file PDF**.
Cukup masukkan **Kode Unik**, sistem akan:

1. Mencari dokumen yang cocok di blockchain
2. Menemukan file PDF terenkripsi di folder `secured/`
3. Memverifikasi integritas file (SHA-256)
4. Membuka PDF dengan kode unik sebagai password
5. Menampilkan data pemilik (Nama, NIM, waktu daftar)

---

## Alur Lengkap

### A. Saat Registrasi (Tab Upload — sudah jalan)

```
Upload PDF → Masukkan Kode Unik, Nama, NIM
    ↓
pdf_secure.stamp_and_secure(src, out, code, name, sid)
    ├─ Beri cap visual "SECURECHAIN VERIFIED"
    ├─ Sisipkan metadata (HMAC-SHA256)
    └─ Enkripsi AES-256 PDF dengan password = kode unik
    ↓
scdv_core.register(out, code, name, sid)
    ├─ Compute SHA-256 file
    ├─ Enkripsi kode unik (AES-256-CBC, MASTER_KEY)
    └─ Simpan block ke blockchain.json
    ↓
Copy file ke secured/{hash}.pdf
    └─ Simpan manifest.json: code → hash, nama, nim
```

**Kunci:** File di `secured/` **dinamai dengan hash-nya** (bukan NIM/nama) →
anonim. Tidak ada yang bisa menghubungkan file ke pemilik tanpa kode unik.

### B. Saat Verifikasi (Tab Verifikasi — HANYA Kode Unik)

```
Masukkan Kode Unik
    ↓
scdv_core.find_by_label(code)   ← sudah ada
    ├─ C++ decrypt setiap encrypted_label di blockchain
    ├─ Cocokkan dengan input code
    └─ Return: STATUS=FOUND + NAME, ID, TIME, HASH
    ↓
Cari file: secured/{HASH}.pdf
    ↓
Verifikasi:
    ├─ pdf_secure.can_open(file_path, code)  → True/False
    └─ (Opsional) Re-hash file & bandingkan dengan blockchain
    ↓
Tampilkan hasil:
    ✅ TERVERIFIKASI: Nama, NIM, Waktu, Buka PDF ✓
    ❌ Kode tidak ditemukan / File tidak ada
```

---

## File yang Perlu Diubah

| File | Perubahan |
|------|-----------|
| `gui/app.py` | **Hapus** `v_file` Entry + tombol "Pilih…" + label "File ijazah" dari `_verify_tab`. Hanya sisakan `v_code` + tombol "Verifikasi" + panel hasil + tombol "Buka Berkas PDF" + tombol "Cek Integritas". |
| `gui/app.py:_verify_worker` | Hapus cabang `if path:` → path sudah tidak ada, hanya pakai `find_by_label(code)` |
| `gui/app.py:_verify_done` | Jika VERIFIED + file bisa dibuka → tampilkan tombol "🔓 Buka Berkas PDF" |
| `gui/app.py` | Fungsi `_pick_verify` bisa dihapus (sudah tidak dipakai) |

---

## Yang SUDAH ADA (tidak perlu diubah)

| Komponen | File | Status |
|----------|------|--------|
| `Block::find_by_label()` | `blockchain.cpp:103-115` | ✅ |
| `cli_find(label)` | `main.cpp:163-181` | ✅ |
| CLI command `"find"` | `main.cpp:198-199` | ✅ |
| `scdv_core.find_by_label(code)` | `scdv_core.py:43-45` | ✅ |
| File naming pakai hash | `app.py:221` | ✅ |
| Buka PDF button | `app.py:141-145, 355-363` | ✅ |

---

## Keamanan Model Baru

```
┌──────────────────────────────────────────────────────────────────────┐
│                     VERIFIKASI TANPA FILE UPLOAD                      │
├──────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  Input: Kode Unik    ──→   Cari di blockchain (decrypt label)        │
│                                 │                                     │
│                                 ▼                                     │
│                      Dapat: file_hash + student data                  │
│                                 │                                     │
│                                 ▼                                     │
│             Cari file secured/{file_hash}.pdf                         │
│                                 │                                     │
│                                 ▼                                     │
│          Coba buka PDF dengan kode unik sebagai password              │
│          (AES-256 native PDF — hanya kode benar yang bisa buka)       │
│                                 │                                     │
│                    ┌────────────┴────────────┐                        │
│                    ▼                         ▼                        │
│           ✅ Bisa dibuka               ❌ Tidak bisa buka             │
│           → Data pemilik               → "Kode salah /               │
│             ditampilkan                   file tidak ditemukan"       │
│           → Tombol "Buka PDF"                                        │
│             muncul                                                    │
└──────────────────────────────────────────────────────────────────────┘
```

---

## UI Sebelum vs Sesudah

### Sekarang (Tab Verifikasi):

```
File ijazah (PDF): [_______________] [Pilih…] (opsional)
Kode Unik      : [_______________]

[🔍 Verifikasi Sekarang]

         [Hasil]

[Cek Integritas Blockchain]
```

### Sesudah (Tab Verifikasi):

```
Kode Unik: [_______________]

[🔍 Verifikasi Sekarang]

  [Hasil + 🔓 Buka Berkas PDF (jika verified)]

[Cek Integritas Blockchain]
```

---

## Edge Cases

| Skenario | Perilaku |
|----------|----------|
| Kode unik tidak ditemukan di blockchain | `STATUS=NOT_FOUND` → "❌ Kode tidak dikenal" |
| Kode unik ditemukan tapi file `secured/{hash}.pdf` hilang | `STATUS=ERROR` → "File tidak ditemukan di penyimpanan" |
| Kode unik ditemukan, file ada, tapi kode salah buka PDF | Status tetap "VERIFIED" (data cocok) tapi `opens=False` → "File tidak bisa dibuka dengan kode ini" |
| File di `secured/` sudah diubah (hash beda) | Re-hash file → bandingkan dengan `block.file_hash` → jika beda, tampilkan peringatan |

---

## Prioritas Implementasi

1. **Hapus file upload dari tab verifikasi** (GUI saja, 15 menit)
2. **Rapikan `_verify_worker`** — hapus cabang path, hanya pakai `find_by_label` (5 menit)
3. **Test end-to-end:** Daftar dengan kode → verifikasi dengan kode (10 menit)
4. **Test security:** Coba verifikasi dengan kode salah → harus gagal (5 menit)