"""
Jembatan ke inti C++ (scdv_verifier.exe).
Semua operasi blockchain/hash/enkripsi-label dikerjakan oleh C++ — modul ini
hanya memanggil exe dan mem-parse output KEY=VALUE-nya.
"""
import os
import subprocess

# Root proyek = folder induk dari folder gui/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "scdv_verifier.exe")


class CoreError(Exception):
    pass


def _run(args):
    if not os.path.exists(EXE):
        raise CoreError(
            f"scdv_verifier.exe tidak ditemukan di:\n{EXE}\n\n"
            "Build dulu core C++ dengan: build_gcc.sh (atau build_gcc.bat)"
        )
    proc = subprocess.run(
        [EXE] + args,
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        cwd=ROOT,  # penting: C++ baca/tulis data/blockchain.json relatif ke root
    )
    data = {}
    for line in proc.stdout.splitlines():
        if "=" in line:
            key, _, val = line.partition("=")
            if key and key.replace("_", "").isalpha() and key.isupper():
                data[key] = val
    return data


def register(pdf_path, code, name, student_id):
    """Daftarkan PDF (yang sudah diamankan) ke blockchain via C++."""
    return _run(["register", pdf_path, code, name, student_id])


def verify(pdf_path, code):
    """Verifikasi PDF + kode unik via C++. -> dict berisi STATUS, NAME, ID, TIME."""
    return _run(["verify", pdf_path, code])


def validate():
    """Validasi integritas seluruh chain via C++."""
    return _run(["validate"])
