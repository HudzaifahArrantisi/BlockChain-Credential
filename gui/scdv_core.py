import json
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "scdv_verifier.exe")
BLOCKCHAIN_FILE = os.path.join(ROOT, "data/blockchain.json")


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
        cwd=ROOT,
    )
    data = {}
    for line in proc.stdout.splitlines():
        if "=" in line:
            key, _, val = line.partition("=")
            if key and key.replace("_", "").isalpha() and key.isupper():
                data[key] = val
    return data


def register(pdf_path, code, name, student_id, details=""):
    if details:
        return _run(["register", pdf_path, code, name, student_id, details])
    return _run(["register", pdf_path, code, name, student_id])


def verify(pdf_path, code):
    return _run(["verify", pdf_path, code])


def find_by_label(code):
    return _run(["find", code])


def find_by_student(name, student_id):
    try:
        return _run(["find_student", name, student_id])
    except Exception:
        return _find_by_student_py(name, student_id)


def _find_by_student_py(name, student_id):
    if not os.path.exists(BLOCKCHAIN_FILE):
        return {"STATUS": "ERROR", "MESSAGE": "blockchain.json not found"}
    try:
        with open(BLOCKCHAIN_FILE, 'r') as f:
            data = json.load(f)
        for block in data.get("blocks", []):
            if (block.get("student_name", "").lower() == name.lower()
                    and block.get("student_id", "") == student_id):
                return {
                    "STATUS": "FOUND",
                    "NAME": block["student_name"],
                    "ID": block["student_id"],
                    "TIME": block.get("timestamp", ""),
                    "HASH": block.get("file_hash", ""),
                    "DETAILS": "",  # encrypted_details can't be decrypted without C++
                }
        return {"STATUS": "NOT_FOUND"}
    except Exception as e:
        return {"STATUS": "ERROR", "MESSAGE": str(e)}


def validate():
    return _run(["validate"])
