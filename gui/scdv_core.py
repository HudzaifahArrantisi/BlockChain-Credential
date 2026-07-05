import json
import os
import subprocess
import urllib.request
import urllib.error
import glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "scdv_verifier.exe")
BLOCKCHAIN_FILE = os.path.join(ROOT, "data/blockchain.json")
NODE_PORTS = {"ugm": 8545, "ui": 8546, "itb": 8547}


class CoreError(Exception):
    pass


def _run(args):
    if not os.path.exists(EXE):
        raise CoreError(
            f"scdv_verifier.exe tidak ditemukan di:\n{EXE}\n\n"
            "Build dulu core C++ dengan: build_gcc.sh (atau BUILD.bat)"
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
                    "DETAILS": "",
                }
        return {"STATUS": "NOT_FOUND"}
    except Exception as e:
        return {"STATUS": "ERROR", "MESSAGE": str(e)}


def validate():
    return _run(["validate"])


def _get_peer_endpoints():
    """Discover consortium node endpoints from .keystore and data/node_* configs."""
    peers = []
    # Look for node config dirs
    node_dirs = glob.glob(os.path.join(ROOT, "data", "node_*"))
    for nd in node_dirs:
        cfg_path = os.path.join(nd, "node_config.json")
        if os.path.exists(cfg_path):
            try:
                with open(cfg_path) as f:
                    cfg = json.load(f)
                addr = cfg.get("listen_addr", "")
                if addr:
                    port = addr.split(":")[-1]
                    peers.append(f"127.0.0.1:{port}")
            except Exception:
                pass
    # Also try known ports
    for label, port in NODE_PORTS.items():
        ep = f"127.0.0.1:{port}"
        if ep not in peers:
            peers.append(ep)
    # Remove duplicates
    seen = set()
    return [p for p in peers if not (p in seen or seen.add(p))]


def multi_verify(file_path=None, label=None, name=None, student_id=None):
    """
    Multi-node consensus verification.
    Queries ALL consortium nodes and returns result with consensus info.
    
    Returns dict with:
      - STATUS: 'VERIFIED' | 'NOT_FOUND' | 'CONSENSUS_FAILED'
      - NAME, ID, TIME, HASH (if found)
      - NODES: list of {endpoint, status, name, id, vault_ok}
      - CONSENSUS: verified_count / total
    """
    peers = _get_peer_endpoints()
    
    result = {
        "STATUS": "ERROR",
        "NODES": [],
        "CONSENSUS": "0/0",
        "NAME": "",
        "ID": "",
        "TIME": "",
        "HASH": "",
    }

    # Build HTTP request
    req_body = {"label": label or ""}
    if file_path and os.path.exists(file_path):
        req_body["file_path"] = file_path
    if name:
        req_body["name"] = name
    if student_id:
        req_body["student_id"] = student_id

    body = json.dumps(req_body)

    responses = []
    for peer in peers:
        url = f"http://{peer}/api/blockchain/verify"
        try:
            req = urllib.request.Request(
                url,
                data=body.encode(),
                headers={"Content-Type": "application/json"},
            )
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode())
                data["endpoint"] = peer
                responses.append(data)
        except Exception as e:
            responses.append({"endpoint": peer, "status": "NO_RESPONSE", "error": str(e)})

    # If no peers respond, fall back to local CLI
    if not responses:
        return find_by_label(label)

    # Tally
    verified_count = 0
    total = len(responses)
    result["NODES"] = responses

    for r in responses:
        if r.get("status") == "VERIFIED":
            verified_count += 1
            if not result["NAME"]:
                result["NAME"] = r.get("name", "")
                result["ID"] = r.get("id", "")
                result["TIME"] = r.get("timestamp", "")
                result["HASH"] = r.get("file_hash", "")

    threshold = (total // 2) + 1  # majority
    result["CONSENSUS"] = f"{verified_count}/{total}"

    if verified_count >= threshold:
        result["STATUS"] = "VERIFIED"
    elif verified_count > 0:
        result["STATUS"] = "PARTIAL"
    else:
        result["STATUS"] = "NOT_FOUND"

    return result
