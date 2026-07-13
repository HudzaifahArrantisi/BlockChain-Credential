#!/usr/bin/env python3
"""
SecureChain Live Visualization Server
Polls blockchain nodes, streams to browser via SSE + D3.js
"""
import http.server
import json
import time
import threading
import urllib.request
import urllib.error
import socketserver
import sys
import subprocess
import os
import tempfile
import base64
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# PDF security & registration pipeline
sys.path.insert(0, str(ROOT / "gui"))
import pdf_secure
import scdv_core

SECURED_FOLDER = str(ROOT / "secured")
MANIFEST_FILE  = os.path.join(SECURED_FOLDER, "manifest.json")

PORT_RANGE = list(range(8545, 8560))
ACTIVE_NODES = {}  # port -> label (auto-discovered)
NODE_LABELS = {
    8545: "Node 1", 8546: "Node 2", 8547: "Node 3",
    8548: "Node 4", 8549: "Node 5", 8550: "Node 6",
    8551: "Node 7", 8552: "Node 8", 8553: "Node 9",
    8554: "Node 10",
}

DEFAULT_NODES = [{"port": p, "label": NODE_LABELS.get(p, f"Node {p}")} for p in PORT_RANGE]

SSE_CLIENTS = []
SSE_LOCK = threading.Lock()

NODE_LOG = {}
MAX_LOG_LINES = 200
NODE_STATE = []
BLOCKCHAIN_STATE = {"blocks": []}


def fetch_json(url):
    try:
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=2) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def scan_ports(port_list):
    """Scan a list of ports, return ones that respond to /api/node/info"""
    found = {}
    for p in port_list:
        info = fetch_json(f"http://127.0.0.1:{p}/api/node/info")
        if info:
            role = info.get("role", "UNKNOWN")
            label = NODE_LABELS.get(p, f"Node {p}")
            found[p] = label
    return found


NODE_FAIL_COUNT = {}


def poll_nodes():
    global ACTIVE_NODES, NODE_STATE, BLOCKCHAIN_STATE, NODE_FAIL_COUNT

    # Initial scan
    ACTIVE_NODES = scan_ports(PORT_RANGE)
    scan_fast_counter = 0

    while True:
        # ── Quick discovery: scan all ports every 2 cycles (4s) ──────
        scan_fast_counter += 1
        if scan_fast_counter >= 2:
            newly_found = scan_ports(PORT_RANGE)
            for p, lbl in newly_found.items():
                if str(p) not in NODE_LOG:
                    NODE_LOG[str(p)] = []
                if p not in ACTIVE_NODES:
                    ACTIVE_NODES[p] = lbl
                    NODE_FAIL_COUNT[p] = 0
                    print(f"[VIS] New node detected: {lbl}:{p}")
            scan_fast_counter = 0

        # ── Poll known nodes ─────────────────────────────────────────
        nodes_data = []
        to_remove = []
        for port, label in list(ACTIVE_NODES.items()):
            info = fetch_json(f"http://127.0.0.1:{port}/api/node/info")
            if info:
                NODE_FAIL_COUNT[port] = 0
                nid = info.get("node_id", f"node_{port}")
                nid_short = nid[:12] + "..." if len(nid) > 15 else nid
                in_election = info.get("election_in_progress", False)
                nodes_data.append({
                    "port": port,
                    "label": label,
                    "id": nid,
                    "id_short": nid_short,
                    "role": info.get("role", "UNKNOWN"),
                    "term": info.get("term", 0),
                    "blocks": info.get("blocks", 0),
                    "leader_id": info.get("leader_id", ""),
                    "listener": info.get("listener", f"0.0.0.0:{port}"),
                    "alive": True,
                    "election_in_progress": in_election,
                })
                ts = time.strftime("%H:%M:%S")
                role = info.get("role", "?")
                term = info.get("term", 0)
                blk = info.get("blocks", 0)
                log_line = f"[{ts}] [{label}:{port}] role={role} term={term} blocks={blk}"
                if str(port) not in NODE_LOG:
                    NODE_LOG[str(port)] = []
                NODE_LOG[str(port)].append(log_line)
                if len(NODE_LOG[str(port)]) > MAX_LOG_LINES:
                    NODE_LOG[str(port)].pop(0)
            else:
                fail_count = NODE_FAIL_COUNT.get(port, 0) + 1
                NODE_FAIL_COUNT[port] = fail_count

        # Determine if any node is currently in an election
        any_election = any(n.get("election_in_progress", False) for n in nodes_data)

        # During an election storm, nodes may briefly not respond.
        # Never remove nodes while an election is in progress — they'll
        # come back once a new leader is settled. Only remove after
        # 6 consecutive failures AND no election activity anywhere.
        if not any_election:
            for port, cnt in list(NODE_FAIL_COUNT.items()):
                if cnt >= 6 and port not in [n["port"] for n in nodes_data]:
                    to_remove.append(port)

        for port in to_remove:
            lbl = ACTIVE_NODES.pop(port, None)
            NODE_FAIL_COUNT.pop(port, None)
            if lbl:
                print(f"[VIS] Node offline (6 failures): {lbl}:{port}")

        NODE_STATE = nodes_data

        leader = next((n for n in nodes_data if n["role"] == "LEADER"), None)
        if leader:
            chain = fetch_json(f"http://127.0.0.1:{leader['port']}/api/blockchain/sync")
            if chain and "blocks" in chain:
                BLOCKCHAIN_STATE = chain

        broadcast_sse()
        time.sleep(2)

        # ── Re-insert stale nodes from fail-count list (they may come back) ─
        for port, cnt in list(NODE_FAIL_COUNT.items()):
            if port not in ACTIVE_NODES and cnt > 0:
                for p in PORT_RANGE:
                    if p == port:
                        ACTIVE_NODES[port] = NODE_LABELS.get(port, f"Node {port}")
                        break


def broadcast_sse():
    payload = json.dumps({
        "nodes": NODE_STATE,
        "logs": dict(NODE_LOG),
        "blockchain": BLOCKCHAIN_STATE,
        "timestamp": time.time(),
    })
    dead = []
    with SSE_LOCK:
        for client in SSE_CLIENTS:
            try:
                client.write(f"data: {payload}\n\n".encode())
                client.flush()
            except Exception:
                dead.append(client)
        for c in dead:
            try:
                SSE_CLIENTS.remove(c)
            except ValueError:
                pass


def broadcast_workflow_event(event_type, data):
    """Send a workflow_progress SSE event to all connected clients (real-time)."""
    dead = []
    with SSE_LOCK:
        for client in SSE_CLIENTS:
            try:
                msg = f"event: workflow_progress\ndata: {json.dumps(data)}\n\n"
                client.write(msg.encode())
                client.flush()
            except Exception:
                dead.append(client)
        for c in dead:
            try:
                SSE_CLIENTS.remove(c)
            except ValueError:
                pass


class SSEHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT / "gui" / "web"), **kwargs)

    def do_GET(self):
        if self.path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            with SSE_LOCK:
                SSE_CLIENTS.append(self.wfile)
            try:
                while True:
                    time.sleep(10)
                    try:
                        self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
                    except Exception:
                        break
            finally:
                with SSE_LOCK:
                    try:
                        SSE_CLIENTS.remove(self.wfile)
                    except ValueError:
                        pass
            return
        if self.path == "/api/nodes":
            self.send_json(json.dumps(DEFAULT_NODES))
            return
        if self.path == "/api/state":
            self.send_json(json.dumps({
                "nodes": NODE_STATE,
                "logs": dict(NODE_LOG),
                "blockchain": BLOCKCHAIN_STATE,
                "timestamp": time.time(),
            }))
            return
        if self.path == "/dev":
            self.send_response(302)
            self.send_header("Location", "/dev.html")
            self.end_headers()
            return
        return super().do_GET()

    def send_json(self, payload):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            self.wfile.write(payload.encode())
        except Exception:
            pass

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except ConnectionError:
            pass

    def do_POST(self):
        if self.path == "/api/register":
            self.handle_register()
            return
        self.send_response(404)
        self.end_headers()

    def handle_register(self):
        """Full registration pipeline: stamp PDF -> encrypt -> blockchain -> save to data/uploads/."""
        try:
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            data = json.loads(body)

            filename = data.get("filename", "upload.pdf")
            file_b64 = data.get("file_data", "")
            kode = data.get("kode", "")
            nama = data.get("nama", "")
            nim = data.get("nim", "")

            if not all([file_b64, kode, nama, nim]):
                self.send_json(json.dumps({"status": "ERROR", "message": "Missing fields"}))
                return

            # Kenali leader & follower dari state saat ini
            global NODE_STATE
            leader_port = None
            follower_ports = []
            for n in NODE_STATE:
                if n["alive"] and n["role"] == "LEADER":
                    leader_port = n["port"]
                elif n["alive"] and n["role"] == "FOLLOWER":
                    follower_ports.append(n["port"])
            if leader_port is None:
                # Fallback: cari node pertama yang alive
                alive_ports = [n["port"] for n in NODE_STATE if n["alive"]]
                if alive_ports:
                    leader_port = alive_ports[0]
                    follower_ports = alive_ports[1:]

            # 0. Workflow event — upload diterima
            broadcast_workflow_event("upload_received", {
                "step": "upload_received",
                "message": f"File {filename} diterima dari user"
            })

            # 1. Save uploaded original file
            file_bytes = base64.b64decode(file_b64)
            upload_dir = ROOT / "data" / "uploads"
            upload_dir.mkdir(parents=True, exist_ok=True)
            src_path = upload_dir / filename
            with open(src_path, "wb") as f:
                f.write(file_bytes)

            # 2. Workflow event — leader memproses
            broadcast_workflow_event("leader_received", {
                "step": "leader_received",
                "port": leader_port,
                "message": f"Leader :{leader_port} menerima file {filename}"
            })

            # 3. Stamp + encrypt via pdf_secure (visual stamp QR + HMAC + AES-256 password)
            broadcast_workflow_event("broadcasting", {
                "step": "broadcasting",
                "from": leader_port,
                "to": follower_ports,
                "message": f"Leader :{leader_port} broadcast ke {len(follower_ports)} follower"
            })
            secured_dir = upload_dir
            temp_secured = os.path.join(str(secured_dir), f"temp_{nim}_SECURED.pdf")
            sig = pdf_secure.stamp_and_secure(str(src_path), temp_secured, kode, nama, nim)

            # 4. Workflow event — follower signing (real-time, satu per satu)
            for idx, fport in enumerate(follower_ports):
                broadcast_workflow_event("follower_signed", {
                    "step": "follower_signed",
                    "port": fport,
                    "total": len(follower_ports),
                    "message": f"Follower :{fport} menandatangani ({idx + 1}/{len(follower_ports)})"
                })
                time.sleep(0.3)

            # 5. Register on blockchain via C++ exe (hashing the secured PDF)
            res = scdv_core.register(temp_secured, kode, nama, nim)
            status = "OK" if res.get("STATUS") == "OK" else "ERROR"
            file_hash = res.get("HASH", "")

            # 6. Workflow event — block committed
            broadcast_workflow_event("block_committed", {
                "step": "block_committed",
                "hash": file_hash,
                "message": f"Block #{res.get('INDEX', '?')} disimpan di ledger"
            })

            # 7. Rename temp -> permanent secured file in data/uploads/
            secured_path = temp_secured
            if file_hash:
                secured_fn = f"{file_hash}_secured.pdf"
                secured_path = os.path.join(str(secured_dir), secured_fn)
                shutil.copy2(temp_secured, secured_path)
                try:
                    os.remove(temp_secured)
                except OSError:
                    pass

            # 5. Also copy to secured/ manifest
            os.makedirs(SECURED_FOLDER, exist_ok=True)
            manifest_secured = os.path.join(SECURED_FOLDER, os.path.basename(secured_path))
            shutil.copy2(secured_path, manifest_secured)

            # 6. Write manifest entry
            manifest = {}
            try:
                if os.path.exists(MANIFEST_FILE):
                    with open(MANIFEST_FILE, "r") as f:
                        manifest = json.load(f)
            except Exception:
                manifest = {}
            manifest[nim] = {
                "name": nama, "code": kode, "hash": file_hash,
                "signature": sig, "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
                "filename": os.path.basename(secured_path),
            }
            with open(MANIFEST_FILE, "w") as f:
                json.dump(manifest, f, indent=2)

            # 7. Broadcast log to SSE
            ts = time.strftime("%H:%M:%S")
            log_line = f"[{ts}] [WEB] Register: {nama} ({nim}) \u2192 {status} hash={file_hash[:16] if file_hash else '?'}"
            for port in list(ACTIVE_NODES.keys()):
                if str(port) not in NODE_LOG:
                    NODE_LOG[str(port)] = []
                NODE_LOG[str(port)].append(log_line)

            # 8. Return confirmation (no file download)
            self.send_json(json.dumps({
                "status": status,
                "message": (
                    f"BERHASIL! {nama} ({nim}) terdaftar di blockchain.\n"
                    f"File aman: {os.path.basename(secured_path)}\n"
                    f"Kode unik: {kode}\n"
                    f"Hash: {file_hash[:20]}..." if status == "OK"
                    else f"Gagal registrasi blockchain"
                ),
                "hash": file_hash,
                "signature": sig,
            }))

        except subprocess.TimeoutExpired:
            self.send_json(json.dumps({"status": "ERROR", "message": "CLI timeout (30s)"}))
        except Exception as ex:
            self.send_json(json.dumps({"status": "ERROR", "message": str(ex)}))

    def log_message(self, fmt, *args):
        pass


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    port = 8080 if len(sys.argv) < 2 else int(sys.argv[1])
    threading.Thread(target=poll_nodes, daemon=True).start()
    httpd = ThreadedHTTPServer(("0.0.0.0", port), SSEHandler)
    print(f"[VIS] Dashboard: http://127.0.0.1:{port}")
    print(f"[VIS] Polling {len(DEFAULT_NODES)} nodes: {', '.join(str(n['port']) for n in DEFAULT_NODES)}")
    print("[VIS] Press Ctrl+C to stop")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[VIS] Stopped")


if __name__ == "__main__":
    main()
