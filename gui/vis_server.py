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
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

DEFAULT_NODES = [
    {"port": 8545, "label": "Node A"},
    {"port": 8546, "label": "Node B"},
    {"port": 8547, "label": "Node C"},
    {"port": 8548, "label": "Node D"},
    {"port": 8549, "label": "Node E"},
    {"port": 8550, "label": "Node F"},
    {"port": 8551, "label": "Node G"},
    {"port": 8552, "label": "Node H"},
    {"port": 8553, "label": "Node I"},
    {"port": 8554, "label": "Node J"},
]

SSE_CLIENTS = []
SSE_LOCK = threading.Lock()

NODE_LOG = {str(n["port"]): [] for n in DEFAULT_NODES}
MAX_LOG_LINES = 200
NODE_STATE = []
BLOCKCHAIN_STATE = {"blocks": []}


def fetch_json(url):
    try:
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=3) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def poll_nodes():
    global NODE_STATE, BLOCKCHAIN_STATE
    while True:
        nodes_data = []
        for nd in DEFAULT_NODES:
            port = nd["port"]
            info = fetch_json(f"http://127.0.0.1:{port}/api/node/info")
            if info:
                nid = info.get("node_id", f"node_{port}")
                nid_short = nid[:12] + "..." if len(nid) > 15 else nid
                nodes_data.append({
                    "port": port,
                    "label": nd["label"],
                    "id": nid,
                    "id_short": nid_short,
                    "role": info.get("role", "UNKNOWN"),
                    "term": info.get("term", 0),
                    "blocks": info.get("blocks", 0),
                    "leader_id": info.get("leader_id", ""),
                    "listener": info.get("listener", f"0.0.0.0:{port}"),
                    "alive": True,
                })
                ts = time.strftime("%H:%M:%S")
                role = info.get("role", "?")
                term = info.get("term", 0)
                blk = info.get("blocks", 0)
                log_line = f"[{ts}] [{nd['label']}:{port}] role={role} term={term} blocks={blk}"
                NODE_LOG[str(port)].append(log_line)
                if len(NODE_LOG[str(port)]) > MAX_LOG_LINES:
                    NODE_LOG[str(port)].pop(0)
            else:
                nodes_data.append({
                    "port": port, "label": nd["label"],
                    "id": "", "id_short": "",
                    "role": "OFFLINE", "term": 0, "blocks": 0,
                    "leader_id": "", "listener": f"0.0.0.0:{port}",
                    "alive": False,
                })
        NODE_STATE = nodes_data

        leader = next((n for n in nodes_data if n["role"] == "LEADER"), None)
        if leader:
            chain = fetch_json(f"http://127.0.0.1:{leader['port']}/api/blockchain/sync")
            if chain and "blocks" in chain:
                BLOCKCHAIN_STATE = chain

        broadcast_sse()
        time.sleep(2)


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
