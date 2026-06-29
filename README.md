# SecureChain Diploma Verifier (SCDV)

Blockchain-based diploma verification with SHA-256 integrity + AES-256 ownership.

## System Flow

```
                  ┌──────────────────────────┐
                  │     Ijazah Document      │
                  │   (PDF/File/Ijazah)   │
                  └────────┬─────────────────┘
                           │
              ┌────────────┴────────────┐
              │  SHA-256 Hash File      │
              │  → file_hash            │
              └────────────┬────────────┘
                           │
              ┌────────────┴────────────┐
              │  Create Block            │
              │  - file_hash             │
              │  - encrypted_label (AES) │
              │  - student data          │
              │  - linked to prev block  │
              └────────────┬────────────┘
                           │
              ┌────────────┴────────────┐
              │  Persist to              │
              │  data/blockchain.json    │
              └─────────────────────────┘
```

## Double-Lock Verification

When verifying, a block passes only if **both** locks pass:

```
Lock 1: SHA-256
  File dimasukkan → hash ulang → cocok dengan hash di block?
  ── Jika beda 1 bit → hash berubah → GAGAL

Lock 2: AES-256
  Unique label dienkripsi di block → decrypt dengan master key
  ── Label yang dimasukkan cocok dengan hasil dekripsi?
```

## Alur Lengkap

### Register (Admin)

```
Admin → pilih file diploma + unique label + data mahasiswa
     → sistem hash file (SHA-256)
     → encrypt unique label (AES-256)
     → buat block baru (terkait hash block sebelumnya)
     → simpan ke blockchain.json
```

### Verify (User / Public)

```
User → pilih file diploma + unique label
    → sistem hash file ulang
    → decrypt semua label di blockchain
    → cocokkan file_hash && decrypted_label
    → VERIFIED atau TAMPERED
```

### Validasi Chain

```
Sistem → loop semua block
      → hitung ulang hash setiap block
      → cocokkan block_hash dan previous_hash
      → valid atau chain rusak
```

## Security Layers

| Layer | Teknologi | Fungsi |
|-------|-----------|--------|
| Integrity | SHA-256 | File tidak diubah |
| Ownership | AES-256-CBC | Hanya pemilik label yang benar |
| Chain | Linked Hash | Blockchain tidak bisa di-replay |
| Persistence | JSON | Data portabel, audit-able |

## Quick Start

```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
./build/scdv_verifier
```

Lihat `BUILD_INSTRUCTIONS.md` untuk detail build per platform.

## Diagram File

```
src/
├── main.cpp               CLI entry + menu loop
├── blockchain.cpp         Chain logic + JSON persistence
├── crypto_utils.cpp       SHA-256 + AES-256 via OpenSSL
└── document_handler.cpp   File I/O, compute hash

include/                   (headers, 1:1 dengan src)
data/blockchain.json       Ledger persistent (gitignored)
```
