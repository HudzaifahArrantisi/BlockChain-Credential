#!/usr/bin/env python3
"""
SCDV Consortium Network Test Harness (v2.0 off-chain vault + multi-sig).
Starts 3 local nodes (UGM:8545, UI:8546, ITB:8547), registers a diploma,
verifies off-chain vault integrity, and demonstrates multi-sig block proposal.
"""

import subprocess
import sys
import os
import time
import json
import signal
import threading
import urllib.request
import urllib.error
import tempfile
import shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXE = os.path.join(ROOT, "scdv_verifier.exe")
DATA_DIR = os.path.join(ROOT, "data")

NODES = {
    "ugm": {"port": 8545, "config": os.path.join(ROOT, "data", "node_ugm", "node_config.json")},
    "ui":  {"port": 8546, "config": os.path.join(ROOT, "data", "node_ui", "node_config.json")},
    "itb": {"port": 8547, "config": os.path.join(ROOT, "data", "node_itb", "node_config.json")},
}

CLEANUP_DIRS = [
    os.path.join(ROOT, ".keystore"),
    os.path.join(ROOT, "data", "offchain_vault"),
]


def ensure_dir(d):
    os.makedirs(d, exist_ok=True)


def run_cli(*args):
    """Run the C++ CLI and return (returncode, stdout)."""
    cmd = [EXE] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return r.returncode, r.stdout, r.stderr


def http_get(port, path):
    try:
        r = urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=3)
        return json.loads(r.read().decode())
    except Exception as e:
        return {"error": str(e)}


def http_post(port, path, body):
    try:
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}{path}",
            data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"},
        )
        r = urllib.request.urlopen(req, timeout=3)
        return json.loads(r.read().decode())
    except Exception as e:
        return {"error": str(e)}


def check_node_ready(port, retries=20, delay=1):
    for i in range(retries):
        info = http_get(port, "/api/node/info")
        if "node_id" in info:
            return info
        time.sleep(delay)
    return None


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    RESET = "\033[0m"


def status(msg, ok=True):
    icon = f"{Colors.GREEN}[OK]{Colors.RESET}" if ok else f"{Colors.RED}[FAIL]{Colors.RESET}"
    print(f"  {icon} {msg}")


def header(title):
    print(f"\n{Colors.CYAN}{'='*60}{Colors.RESET}")
    print(f"{Colors.CYAN}  {title}{Colors.RESET}")
    print(f"{Colors.CYAN}{'='*60}{Colors.RESET}")


def main():
    print(f"{Colors.YELLOW}SCDV v2.0 Consortium Network Test{Colors.RESET}")
    print(f"ROOT: {ROOT}")
    print(f"EXE:  {EXE}")

    # Step 1: Generate keys for all 3 nodes
    header("Step 1: Consortium Key Generation")
    rc, out, err = run_cli("--multi-keygen", "ugm", "ui", "itb")
    if rc != 0:
        print(f"  {Colors.RED}Keygen failed: {err}{Colors.RESET}")
        return 1
    status("Generated keys for ugm, ui, itb")

    # Step 2: Create configs for each node
    header("Step 2: Creating Node Configs")
    for label, info in NODES.items():
        node_dir = os.path.join(DATA_DIR, f"node_{label}")
        ensure_dir(node_dir)
        config_path = info["config"]

        # Inject keys from keystore into config
        rc, out, err = run_cli("--keygen", node_dir)
        status(f"Config created for {label} at {config_path}")

    # Now read the keypairs and write proper configs
    for label, info in NODES.items():
        node_dir = os.path.join(DATA_DIR, f"node_{label}")
        config_path = info["config"]

        # Read from keystore
        keystore_path = os.path.join(ROOT, ".keystore", f"{label}.key")
        if os.path.exists(keystore_path):
            with open(keystore_path) as f:
                kp = json.load(f)
        else:
            print(f"  {Colors.RED}Keystore key not found for {label}{Colors.RESET}")
            return 1

        # Read default seed peers (other nodes)
        seed_peers = []
        for other_label, other_info in NODES.items():
            if other_label != label:
                seed_peers.append(f"127.0.0.1:{other_info['port']}")

        cfg = {
            "priv_key": kp["priv_key"],
            "pub_key": kp["pub_key"],
            "listen_addr": f"0.0.0.0:{info['port']}",
            "seed_peers": seed_peers,
        }
        with open(config_path, "w") as f:
            json.dump(cfg, f, indent=2)
        status(f"Config written for {label} (port {info['port']})")

    # Step 3: Clear old blockchain data
    header("Step 3: Clean Slate")
    for label in NODES:
        bc_path = os.path.join(DATA_DIR, f"node_{label}", "blockchain.json")
        if os.path.exists(bc_path):
            os.remove(bc_path)

    # Remove old blockchain.json in root data dir
    root_bc = os.path.join(DATA_DIR, "blockchain.json")
    if os.path.exists(root_bc):
        os.remove(root_bc)

    # Clean offchain vault
    vault_dir = os.path.join(ROOT, "data", "offchain_vault")
    if os.path.exists(vault_dir):
        shutil.rmtree(vault_dir)
    status("Cleared old data")

    # Step 4: Start 3 nodes
    header("Step 4: Starting Consortium Nodes")
    processes = []
    log_dir = os.path.join(ROOT, "data", "logs")
    ensure_dir(log_dir)

    for label, info in NODES.items():
        log_path = os.path.join(log_dir, f"node_{label}.log")
        log_file = open(log_path, "w")
        p = subprocess.Popen(
            [EXE, "--node", info["config"]],
            cwd=ROOT,
            stdout=log_file,
            stderr=subprocess.STDOUT,
        )
        processes.append((label, p, log_file))
        print(f"  Started {label} (PID {p.pid}) -> log: {log_path}")

    # Wait for nodes to stabilize
    print(f"\n  {Colors.YELLOW}Waiting for nodes to stabilize (8s)...{Colors.RESET}")
    time.sleep(8)

    # Check nodes are running
    header("Step 5: Verify Node Readiness")
    node_infos = {}
    for label, info in NODES.items():
        ninfo = check_node_ready(info["port"])
        if ninfo and "node_id" in ninfo:
            node_infos[label] = ninfo
            status(f"{label} @ {info['port']}: role={ninfo.get('role','?')} term={ninfo.get('term','?')}")
        else:
            status(f"{label} @ {info['port']}: NOT RESPONDING", ok=False)

    if len(node_infos) < 2:
        print(f"\n  {Colors.RED}Not enough nodes responding. Aborting.{Colors.RESET}")
        cleanup(processes)
        return 1

    # Step 6: Register a diploma
    header("Step 6: Register Diploma (via first available node)")

    leader_label = None
    leader_info = None
    for label, info in NODES.items():
        if label in node_infos and node_infos[label].get("role") == "LEADER":
            leader_label = label
            leader_info = info
            break

    if not leader_label:
        # Pick the first responsive node
        for label, info in NODES.items():
            if label in node_infos:
                leader_label = label
                leader_info = info
                break

    print(f"  Using node: {leader_label} @ {leader_info['port']}")

    # Create a sample diploma file
    sample_file = os.path.join(ROOT, "data", "sample_diploma.txt")
    with open(sample_file, "w") as f:
        f.write("DIPLOMA: John Doe (UGM-2024-001) - Bachelor of Computer Science")

    label_str = f"{leader_label.upper()}-2024-001-DOE"
    student_name = "John Doe"
    student_id = f"{leader_label.upper()}-2024-001"
    details = json.dumps({
        "program": "Bachelor of Computer Science",
        "gpa": "3.85",
        "graduation_date": "2024-06-15",
        "photo_hash": "abc123def456",
        "pdf_hash": "789ghi012jkl"
    })

    # Use the --propose-block CLI
    peer_str = ",".join([f"127.0.0.1:{NODES[p]['port']}" for p in NODES if p != leader_label])

    rc, out, err = run_cli(
        "--propose-block",
        sample_file,
        label_str,
        student_name,
        student_id,
        details,
        peer_str,
    )
    print(f"  CLI output: {out.strip()}")
    if rc == 0:
        status("Diploma registered with multi-sig")
    else:
        status(f"Propose failed (rc={rc})", ok=False)

    # Step 7: Verify via each node
    header("Step 7: Cross-Node Verification")
    for label, info in NODES.items():
        bc_sync = http_get(info["port"], "/api/blockchain/sync")
        if "blocks" in bc_sync and len(bc_sync["blocks"]) > 0:
            b = bc_sync["blocks"][0]
            has_details = bool(b.get("details_hash", ""))
            has_sigs = len(b.get("validator_sigs", [])) > 0
            status(f"{label}: block={b.get('index','?')} details_hash={'yes' if has_details else 'no'} validator_sigs={len(b.get('validator_sigs',[]))}")
        else:
            status(f"{label}: no blocks synced", ok=False)

    # Step 8: Validate chain
    header("Step 8: Chain Validation")
    rc, out, err = run_cli("validate")
    status(f"validate = {out.strip()}")

    # Step 9: Check off-chain vault
    header("Step 9: Off-Chain Vault Integrity")
    vault_dir = os.path.join(ROOT, "data", "offchain_vault")
    if os.path.isdir(vault_dir):
        files = os.listdir(vault_dir)
        status(f"Vault files: {len(files)}")
        for fname in files:
            fpath = os.path.join(vault_dir, fname)
            with open(fpath) as f:
                data = json.load(f)
            status(f"  {fname}: student={data.get('student_name','?')}")
    else:
        status("Vault directory not found", ok=False)

    # Step 10: Stop nodes
    header("Step 10: Cleanup")
    cleanup(processes)

    print(f"\n{Colors.GREEN}{'='*60}{Colors.RESET}")
    print(f"{Colors.GREEN}  CONSORTIUM TEST COMPLETED{Colors.RESET}")
    print(f"{Colors.GREEN}{'='*60}{Colors.RESET}")
    return 0


def cleanup(processes):
    for label, p, log_file in processes:
        try:
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
            else:
                p.terminate()
                p.wait(timeout=5)
            log_file.close()
            print(f"  Stopped {label} (PID {p.pid})")
        except Exception as e:
            print(f"  Error stopping {label}: {e}")


if __name__ == "__main__":
    sys.exit(main())
