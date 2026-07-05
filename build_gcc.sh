#!/usr/bin/env bash
# Build inti C++ (scdv_verifier.exe) dengan g++ MinGW — tanpa CMake/Visual Studio.
# Static linking => .exe portabel, jalan tanpa DLL eksternal.
set -e
cd "$(dirname "$0")"

MINGW64_BIN=/c/ProgramData/mingw64/mingw64/bin
export PATH="$MINGW64_BIN:$PATH"

OPT=/c/ProgramData/mingw64/mingw64/opt
[ -f "$OPT/lib/libcrypto.a" ] || { echo "OpenSSL dev (libcrypto.a) tidak ditemukan di $OPT/lib"; exit 1; }

echo "[*] Compiling scdv_verifier.exe (g++ static)…"
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ \
  src/main.cpp src/crypto_utils.cpp src/blockchain.cpp src/document_handler.cpp src/ecdsa_utils.cpp src/node.cpp src/offchain_vault.cpp src/keystore.cpp \
  -I. -Iinclude -I"$OPT/include" \
  "$OPT/lib/libssl.a" "$OPT/lib/libcrypto.a" \
  -lws2_32 -lcrypt32 -lgdi32 -ladvapi32 -luser32 -lz \
  -o scdv_verifier.exe

mkdir -p data
echo "[+] OK -> scdv_verifier.exe"
./scdv_verifier.exe validate >/dev/null 2>&1 && echo "[+] Self-check: exe jalan normal."
