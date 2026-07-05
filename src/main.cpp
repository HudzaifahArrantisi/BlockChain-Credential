#include <iostream>
#include <string>
#include <fstream>
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
static int make_dir(const std::string& path) { return _mkdir(path.c_str()); }
#else
#include <sys/stat.h>
static int make_dir(const std::string& path) { return mkdir(path.c_str(), 0755); }
#endif
#include "include/blockchain.h"
#include "include/document_handler.h"
#include "include/crypto_utils.h"
#include "include/ecdsa_utils.h"
#include "include/node.h"
#include "include/keystore.h"
#include "include/offchain_vault.h"

const std::string BLOCKCHAIN_FILE = "data/blockchain.json";
const std::string NODE_CONFIG_FILE = "data/node_config.json";

static ConsensusNode* g_node = nullptr;

void signal_handler(int) {
    if (g_node) {
        std::cout << "\n[!] Shutting down node..." << std::endl;
        g_node->stop();
    }
    exit(0);
}

// ── Node Configuration ────────────────────────────────────────────────────

struct NodeConfig {
    std::string priv_key;
    std::string pub_key;
    std::string listen_addr = "0.0.0.0:8545";
    std::vector<std::string> seed_peers;

    void save(const std::string& path) {
        json j;
        j["priv_key"] = priv_key;
        j["pub_key"] = pub_key;
        j["listen_addr"] = listen_addr;
        j["seed_peers"] = seed_peers;
        std::ofstream f(path);
        f << j.dump(2);
    }

    static NodeConfig load(const std::string& path) {
        NodeConfig cfg;
        std::ifstream f(path);
        if (f.is_open()) {
            json j;
            f >> j;
            cfg.priv_key = j.value("priv_key", "");
            cfg.pub_key = j.value("pub_key", "");
            cfg.listen_addr = j.value("listen_addr", "0.0.0.0:8545");
            cfg.seed_peers = j.value("seed_peers", json::array()).get<std::vector<std::string>>();
        }
        return cfg;
    }
};

// ── Key Generation ─────────────────────────────────────────────────────────

int cmd_keygen(const std::string& output_dir) {
    std::cout << "[*] Generating ECDSA secp256k1 keypair..." << std::endl;
    auto kp = ECDSAUtils::generate_keypair();

    std::string node_id = ECDSAUtils::pub_key_to_short_id(kp.pub_key_hex);
    std::cout << "[+] Node ID: " << node_id << std::endl;
    std::cout << "[+] Private Key: " << kp.priv_key_hex << std::endl;
    std::cout << "[+] Public Key:  " << kp.pub_key_hex << std::endl;

    // Save to keystore
    Keypair keystore_kp;
    keystore_kp.label = node_id;
    keystore_kp.priv_key_hex = kp.priv_key_hex;
    keystore_kp.pub_key_hex = kp.pub_key_hex;
    Keystore::save_keypair(keystore_kp);

    // Save to data dir node_config.json (backward compat)
    NodeConfig cfg;
    cfg.priv_key = kp.priv_key_hex;
    cfg.pub_key = kp.pub_key_hex;
    cfg.listen_addr = "0.0.0.0:8545";

    std::string config_path = output_dir + "/node_config.json";
    cfg.save(config_path);
    std::cout << "[+] Config saved to " << config_path << std::endl;

    // Save public key separately for sharing
    std::string pubkey_path = output_dir + "/node_" + node_id + ".pub";
    std::ofstream pkf(pubkey_path);
    pkf << kp.pub_key_hex;
    std::cout << "[+] Public key exported to " << pubkey_path << std::endl;

    return 0;
}

int cmd_multikeygen(const std::vector<std::string>& labels) {
    for (const auto& label : labels) {
        std::cout << "[*] Generating key for '" << label << "'..." << std::endl;
        auto kp = ECDSAUtils::generate_keypair();
        std::string node_id = ECDSAUtils::pub_key_to_short_id(kp.pub_key_hex);
        Keypair kp2;
        kp2.label = label;
        kp2.priv_key_hex = kp.priv_key_hex;
        kp2.pub_key_hex = kp.pub_key_hex;
        Keystore::save_keypair(kp2);
        std::cout << "[+] Node ID: " << node_id << std::endl;
        std::cout << "[+] Pub key: " << kp.pub_key_hex.substr(0, 32) << "..." << std::endl;
    }
    return 0;
}

// ── Multi-Node Verify (Consensus Verification) ─────────────────────────────

int cli_multi_verify(const std::string& file_path,
                      const std::string& label,
                      const std::vector<std::string>& peer_endpoints) {
    std::string file_hash;
    if (!file_path.empty() && file_path != "none" && file_path != "-") {
        file_hash = DocumentHandler::compute_file_hash(file_path);
    }

    // Query local node first
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain local_bc(cfg.priv_key, cfg.pub_key);
    local_bc.load_from_file(BLOCKCHAIN_FILE);

    struct VerifyResult {
        std::string node_id;
        std::string status;
        std::string name;
        std::string student_id;
        bool vault_ok = false;
    };
    std::vector<VerifyResult> results;

    // Local check
    const Block* local_block = local_bc.find_by_label(label);
    VerifyResult local_res;
    local_res.node_id = "local";
    if (local_block) {
        local_res.status = "VERIFIED";
        local_res.name = local_block->student_name;
        local_res.student_id = local_block->student_id;
        if (!local_block->details_hash.empty()) {
            local_res.vault_ok = OffChainVault::verify_details(
                local_block->file_hash, local_block->details_hash);
        } else {
            local_res.vault_ok = true; // no off-chain data to verify
        }
        if (!file_hash.empty() && local_block->file_hash != file_hash) {
            local_res.status = "FILE_MISMATCH";
        }
    } else {
        local_res.status = "NOT_FOUND";
    }
    results.push_back(local_res);

    // Query peers
    json req;
    req["file_hash"] = file_hash;
    req["label"] = label;
    std::string body = req.dump();

    for (const auto& peer : peer_endpoints) {
        std::string url = "http://" + peer + "/api/blockchain/verify";
        std::string resp = ConsensusNode::http_post(url, body);

        VerifyResult pr;
        pr.node_id = peer;
        if (resp.empty()) {
            pr.status = "NO_RESPONSE";
        } else {
            try {
                json j = json::parse(resp);
                pr.status = j.value("status", "ERROR");
                pr.name = j.value("name", "");
                pr.student_id = j.value("id", "");
                pr.vault_ok = j.value("vault_integrity", false);
            } catch (...) {
                pr.status = "PARSE_ERROR";
            }
        }
        results.push_back(pr);
    }

    // Tally results
    int verified_count = 0;
    int total = (int)results.size();
    for (const auto& r : results) {
        if (r.status == "VERIFIED") verified_count++;
    }

    int threshold = (total / 2) + 1; // majority

    std::cout << "\n=== MULTI-NODE VERIFICATION ===" << std::endl;
    for (const auto& r : results) {
        std::string icon;
        if (r.status == "VERIFIED") icon = "[OK]";
        else if (r.status == "NOT_FOUND") icon = "[--]";
        else if (r.status == "FILE_MISMATCH") icon = "[XX]";
        else icon = "[!!]";
        std::cout << "  " << icon << " " << r.node_id
                  << " -> " << r.status;
        if (r.status == "VERIFIED") {
            std::cout << " (" << r.name << ")";
            if (r.vault_ok) std::cout << " vault=OK";
            else std::cout << " vault=FAIL";
        }
        std::cout << std::endl;
    }
    std::cout << "  Consensus: " << verified_count << "/" << total
              << " (need " << threshold << ")" << std::endl;

    if (verified_count >= threshold) {
        std::cout << "\nSTATUS=VERIFIED" << std::endl;
        std::cout << "NAME=" << local_res.name << std::endl;
        std::cout << "ID=" << local_res.student_id << std::endl;
        return 0;
    } else {
        std::cout << "\nSTATUS=FAILED" << std::endl;
        std::cout << "MESSAGE=Insufficient consensus (" << verified_count
                  << "/" << total << ")" << std::endl;
        return 1;
    }
}

// ── Propose Block (Multi-sig Flow) ─────────────────────────────────────────

int cli_propose_block(const std::string& file_path, const std::string& label,
                       const std::string& name, const std::string& id,
                       const std::string& details_str,
                       const std::vector<std::string>& peer_endpoints) {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);

    try {
        std::string file_hash = DocumentHandler::compute_file_hash(file_path);
        std::cout << "[PROPOSE] Registering diploma for " << name << " (" << id << ")" << std::endl;

        // Encrypt label
        std::string encrypted_label;
        if (!cfg.pub_key.empty()) {
            encrypted_label = ECDSAUtils::ecdh_aes_encrypt(cfg.pub_key, label);
        } else {
            encrypted_label = CryptoUtils::aes256_encrypt(label, "SecureChain2024!Key@Campus");
        }

        // Create proposal block (saves off-chain details)
        Block proposal = blockchain.prepare_block_proposal(file_hash, encrypted_label,
                                                            name, id, details_str);
        std::string node_id = ECDSAUtils::pub_key_to_short_id(cfg.pub_key);
        std::cout << "[PROPOSE] Block hash: " << proposal.block_hash.substr(0, 16) << "..." << std::endl;

        // Proposer signs first
        proposal.signature = ECDSAUtils::sign(cfg.priv_key, proposal.block_hash);

        // Build proposal JSON
        json proposal_json = proposal.to_json();
        proposal_json["proposer_id"] = node_id;

        // Send to peers for their signature
        std::vector<std::string> collected_sigs;
        collected_sigs.push_back(proposal.signature);  // proposer's sig

        for (const auto& peer : peer_endpoints) {
            std::string url = "http://" + peer + "/api/blockchain/sign_proposal";
            std::cout << "[PROPOSE] Requesting signature from " << peer << "... ";

            std::string resp = ConsensusNode::http_post(url, proposal_json.dump());
            if (resp.empty()) {
                std::cout << "FAILED (no response)" << std::endl;
                continue;
            }

            try {
                json j = json::parse(resp);
                if (j.value("status", "") == "signed") {
                    std::string peer_sig = j.value("signature", "");
                    std::string peer_id = j.value("validator_id", "");
                    if (!peer_sig.empty()) {
                        collected_sigs.push_back(peer_sig);
                        proposal.validator_sigs.push_back(peer_sig);
                        std::cout << "SIGNED by " << peer_id.substr(0, 12) << "..." << std::endl;
                    }
                } else {
                    std::cout << "REJECTED: " << j.value("message", "") << std::endl;
                }
            } catch (...) {
                std::cout << "PARSE ERROR" << std::endl;
            }
        }

        std::cout << "[PROPOSE] Collected " << collected_sigs.size() << " signature(s)" << std::endl;

        if (collected_sigs.size() < 2) {
            std::cout << "[!] Need at least 2 signatures (1 proposer + 1 validator)" << std::endl;
            // Still commit with just the proposer's signature
        }

        // Recalculate block hash with validator_sigs included
        proposal.block_hash = blockchain.calculate_block_hash(proposal);
        // Re-sign with updated hash
        proposal.signature = ECDSAUtils::sign(cfg.priv_key, proposal.block_hash);

        // Commit
        if (blockchain.append_multi_sig_block(proposal)) {
            blockchain.save_to_file(BLOCKCHAIN_FILE);
            std::cout << "[PROPOSE] Block committed. Multi-sig: "
                      << collected_sigs.size() << " sigs" << std::endl;
            std::cout << "STATUS=OK\nHASH=" << file_hash << std::endl;
            return 0;
        } else {
            std::cout << "STATUS=ERROR\nMESSAGE=Block rejected by chain" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "STATUS=ERROR\nMESSAGE=" << e.what() << std::endl;
        return 1;
    }
}

// ── Node Daemon ────────────────────────────────────────────────────────────

int cmd_node(const std::string& config_path) {
    NodeConfig cfg = NodeConfig::load(config_path);
    if (cfg.priv_key.empty() || cfg.pub_key.empty()) {
        std::cerr << "[!] No keys found in " << config_path << std::endl;
        std::cerr << "    Run: scdv_verifier --keygen data/" << std::endl;
        return 1;
    }

    std::string node_id = ECDSAUtils::pub_key_to_short_id(cfg.pub_key);
    std::string data_dir = "data/node_" + node_id;
    make_dir(data_dir);

    std::cout << "[*] Starting SecureChain Node" << std::endl;
    std::cout << "    Node ID: " << node_id << std::endl;
    std::cout << "    Listen:  " << cfg.listen_addr << std::endl;
    std::cout << "    Peers:   " << cfg.seed_peers.size() << std::endl;
    std::cout << "    Data:    " << data_dir << std::endl;

    ConsensusNode node(cfg.listen_addr, data_dir, cfg.priv_key, cfg.pub_key);
    g_node = &node;

    // Register self as the first validator
    node.add_validator(node_id, cfg.pub_key, cfg.listen_addr);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Add seed peers
    for (const auto& peer : cfg.seed_peers) {
        node.add_peer(peer);
    }

    node.start();

    // Discover peers via handshake
    for (const auto& peer : cfg.seed_peers) {
        json handshake;
        handshake["endpoint"] = cfg.listen_addr;
        handshake["pub_key"] = cfg.pub_key;
        std::string resp = ConsensusNode::http_post("http://" + peer + "/api/peer/hello",
                                                     handshake.dump());
        if (!resp.empty()) {
            try {
                json j = json::parse(resp);
                std::string peer_id = j.value("node_id", "");
                std::string peer_pub = j.value("pub_key", "");
                std::cout << "[+] Connected to peer " << peer_id << " @ " << peer << std::endl;
                node.add_peer(peer, peer_pub, peer_id);
                if (!peer_id.empty() && !peer_pub.empty()) {
                    node.add_validator(peer_id, peer_pub, peer);
                    std::cout << "[+] Peer " << peer_id << " registered as validator" << std::endl;
                }
            } catch (...) {}
        }
    }

    // Wait for node to run
    std::cout << "\n[NODE " << node_id << "] Running. Press Ctrl+C to stop.\n" << std::endl;

    // Main loop: print status periodically
    while (node.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::cout << "[STATUS] " << node.get_status_json() << std::endl;
    }

    return 0;
}

// ── CLI: Node status ───────────────────────────────────────────────────────

int cmd_node_status() {
    // Read all node configs and print their blockchains
    try {
        auto kp = ECDSAUtils::generate_keypair();
        StrictBlockchain tmp("", "");
        tmp.load_from_file(BLOCKCHAIN_FILE);
        tmp.print_chain();
    } catch (const std::exception& e) {
        std::cerr << "[!] Error: " << e.what() << std::endl;
    }
    return 0;
}

int cmd_list_validators() {
    StrictBlockchain bc("", "");
    bc.load_from_file(BLOCKCHAIN_FILE);
    auto validators = bc.get_validators();
    std::cout << "Validators: " << validators.size() << std::endl;
    for (const auto& v : validators) {
        std::cout << "  " << v.node_id << " | " << v.endpoint << std::endl;
    }
    return 0;
}

// ── Legacy CLI (unchanged interface, updated internals) ────────────────────

int cli_register(const std::string& path, const std::string& label,
                 const std::string& name, const std::string& id,
                 const std::string& details = "") {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    try {
        std::string file_hash = DocumentHandler::compute_file_hash(path);
        blockchain.register_diploma(file_hash, label, name, id, details);
        blockchain.save_to_file(BLOCKCHAIN_FILE);
        std::cout << "STATUS=OK\nHASH=" << file_hash << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "STATUS=ERROR\nMESSAGE=" << e.what() << std::endl;
        return 1;
    }
}

int cli_verify(const std::string& path, const std::string& label) {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    try {
        std::string file_hash = DocumentHandler::compute_file_hash(path);
        const Block* b = blockchain.verify_and_get(file_hash, label);
        if (b) {
            std::cout << "STATUS=VERIFIED\nNAME=" << b->student_name
                      << "\nID=" << b->student_id
                      << "\nTIME=" << b->timestamp
                      << "\nHASH=" << file_hash;
            if (!b->encrypted_details.empty()) {
                try {
                    std::string decrypted;
                    if (!cfg.priv_key.empty()) {
                        decrypted = ECDSAUtils::ecdh_aes_decrypt(cfg.priv_key, b->encrypted_details);
                    } else {
                        decrypted = CryptoUtils::aes256_decrypt(b->encrypted_details, label);
                    }
                    std::cout << "\nDETAILS=" << decrypted;
                } catch (...) {}
            }
            std::cout << std::endl;
        } else {
            std::cout << "STATUS=FAILED" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cout << "STATUS=ERROR\nMESSAGE=" << e.what() << std::endl;
        return 1;
    }
}

int cli_find(const std::string& label) {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    try {
        const Block* b = blockchain.find_by_label(label);
        if (b) {
            std::cout << "STATUS=FOUND\nNAME=" << b->student_name
                      << "\nID=" << b->student_id
                      << "\nTIME=" << b->timestamp
                      << "\nHASH=" << b->file_hash;
            if (!b->encrypted_details.empty()) {
                try {
                    std::string decrypted;
                    if (!cfg.priv_key.empty()) {
                        decrypted = ECDSAUtils::ecdh_aes_decrypt(cfg.priv_key, b->encrypted_details);
                    } else {
                        decrypted = CryptoUtils::aes256_decrypt(b->encrypted_details, label);
                    }
                    std::cout << "\nDETAILS=" << decrypted;
                } catch (...) {}
            }
            std::cout << std::endl;
        } else {
            std::cout << "STATUS=NOT_FOUND" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cout << "STATUS=ERROR\nMESSAGE=" << e.what() << std::endl;
        return 1;
    }
}

int cli_find_by_student(const std::string& name, const std::string& id) {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    try {
        const Block* b = blockchain.find_by_name_and_id(name, id);
        if (b) {
            std::cout << "STATUS=FOUND\nNAME=" << b->student_name
                      << "\nID=" << b->student_id
                      << "\nTIME=" << b->timestamp
                      << "\nHASH=" << b->file_hash;
            if (!b->encrypted_details.empty()) {
                try {
                    std::string decrypted;
                    if (!cfg.priv_key.empty()) {
                        decrypted = ECDSAUtils::ecdh_aes_decrypt(cfg.priv_key, b->encrypted_details);
                    } else {
                        decrypted = CryptoUtils::aes256_decrypt(b->encrypted_details, b->student_id);
                    }
                    std::cout << "\nDETAILS=" << decrypted;
                } catch (...) {}
            }
            std::cout << std::endl;
        } else {
            std::cout << "STATUS=NOT_FOUND" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cout << "STATUS=ERROR\nMESSAGE=" << e.what() << std::endl;
        return 1;
    }
}

int cli_validate() {
    StrictBlockchain blockchain("", "");
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    std::cout << "STATUS=" << (blockchain.validate_chain() ? "VALID" : "INVALID") << std::endl;
    return 0;
}

// ── Main ───────────────────────────────────────────────────────────────────

void print_banner() {
    std::cout << "\n+==================================================+" << std::endl;
    std::cout << "|  SecureChain Diploma Verifier (SCDV) v2.0        |" << std::endl;
    std::cout << "|  Distributed Blockchain Document Verification    |" << std::endl;
    std::cout << "+==================================================+\n" << std::endl;
}

void print_menu() {
    std::cout << "\n[1] Register Diploma (Admin)" << std::endl;
    std::cout << "[2] Verify Diploma (User)" << std::endl;
    std::cout << "[3] View Blockchain" << std::endl;
    std::cout << "[4] Validate Chain Integrity" << std::endl;
    std::cout << "[5] Node Status" << std::endl;
    std::cout << "[6] List Validators" << std::endl;
    std::cout << "[7] Exit" << std::endl;
    std::cout << "\nSelect option: ";
}

void register_diploma() {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);

    std::string filepath, unique_label, student_name, student_id;

    std::cout << "\n[REGISTRATION MODE - Admin Only]" << std::endl;
    std::cout << "Enter diploma file path: ";
    std::getline(std::cin, filepath);

    if (!DocumentHandler::file_exists(filepath)) {
        std::cerr << "[!] ERROR: File not found: " << filepath << std::endl;
        return;
    }

    std::cout << "Enter Unique Label (e.g., UGM-010203-HUDZAIFAH): ";
    std::getline(std::cin, unique_label);

    std::cout << "Enter Student Name: ";
    std::getline(std::cin, student_name);

    std::cout << "Enter Student ID: ";
    std::getline(std::cin, student_id);

    try {
        std::string file_hash = DocumentHandler::compute_file_hash(filepath);
        std::cout << "[+] File SHA-256: " << file_hash.substr(0, 32) << "..." << std::endl;

        blockchain.register_diploma(file_hash, unique_label, student_name, student_id);
        blockchain.save_to_file(BLOCKCHAIN_FILE);
        std::cout << "[+] SUCCESS: Diploma registered and saved to blockchain!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[!] ERROR: " << e.what() << std::endl;
    }
}

void verify_diploma() {
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    StrictBlockchain blockchain(cfg.priv_key, cfg.pub_key);
    blockchain.load_from_file(BLOCKCHAIN_FILE);

    std::string filepath, unique_label;

    std::cout << "\n[VERIFICATION MODE - Public Access]" << std::endl;
    std::cout << "Enter diploma file path: ";
    std::getline(std::cin, filepath);

    if (!DocumentHandler::file_exists(filepath)) {
        std::cerr << "[!] ERROR: File not found: " << filepath << std::endl;
        return;
    }

    std::cout << "Enter Unique Label: ";
    std::getline(std::cin, unique_label);

    try {
        std::string file_hash = DocumentHandler::compute_file_hash(filepath);
        std::cout << "[+] File SHA-256: " << file_hash.substr(0, 32) << "..." << std::endl;

        if (blockchain.verify_diploma(file_hash, unique_label)) {
            std::cout << "\n+========================================+" << std::endl;
            std::cout << "|  [OK] VERIFIED ALUMNI - DIPLOMA AUTHENTIC |" << std::endl;
            std::cout << "|  Double-Lock Passed:                   |" << std::endl;
            std::cout << "|  [1] File Integrity: SHA-256 OK        |" << std::endl;
            std::cout << "|  [2] Ownership: Unique Label OK        |" << std::endl;
            std::cout << "+========================================+" << std::endl;
        } else {
            std::cout << "\n+========================================+" << std::endl;
            std::cout << "|  [X] VERIFICATION FAILED               |" << std::endl;
            std::cout << "+========================================+" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[!] ERROR: " << e.what() << std::endl;
    }
}

void view_blockchain() {
    StrictBlockchain blockchain("", "");
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    blockchain.print_chain();
}

void validate_integrity() {
    StrictBlockchain blockchain("", "");
    blockchain.load_from_file(BLOCKCHAIN_FILE);

    std::cout << "\n[INTEGRITY VALIDATION]" << std::endl;
    if (blockchain.validate_chain()) {
        std::cout << "[OK] Blockchain is VALID and TAMPER-FREE" << std::endl;
    } else {
        std::cout << "[X] Blockchain validation FAILED - POSSIBLE TAMPERING!" << std::endl;
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    // CLI mode: commands
    if (argc > 1) {
        std::string cmd = argv[1];

        // --keygen <dir> : generate ECDSA keypair
        if (cmd == "--keygen" || cmd == "keygen" || cmd == "--init") {
            std::string dir = (argc > 2) ? argv[2] : "data";
            return cmd_keygen(dir);
        }

        // --node <config> : run as daemon node
        if (cmd == "--node" || cmd == "node" || cmd == "--daemon") {
            std::string cfg = (argc > 2) ? argv[2] : NODE_CONFIG_FILE;
            return cmd_node(cfg);
        }

        // --status : show node/chain status
        if (cmd == "--status" || cmd == "status") {
            return cmd_node_status();
        }

        // --validators : list validators
        if (cmd == "--validators" || cmd == "validators") {
            return cmd_list_validators();
        }

        // Legacy CLI modes (for GUI compatibility)
        if (cmd == "register" && (argc == 6 || argc == 7))
            return cli_register(argv[2], argv[3], argv[4], argv[5], argc == 7 ? argv[6] : "");
        if (cmd == "verify" && argc == 4)
            return cli_verify(argv[2], argv[3]);
        if (cmd == "find" && argc == 3)
            return cli_find(argv[2]);
        if (cmd == "find_student" && argc == 4)
            return cli_find_by_student(argv[2], argv[3]);
        if (cmd == "validate")
            return cli_validate();

        // --multi-keygen : generate keypairs for consortium nodes
        if (cmd == "--multi-keygen") {
            std::vector<std::string> labels;
            for (int i = 2; i < argc; i++) labels.push_back(argv[i]);
            if (labels.empty()) {
                labels = {"ugm", "ui", "itb"};
            }
            return cmd_multikeygen(labels);
        }

        // --multi-verify <file> <label> <peer1,peer2,...>
        if (cmd == "--multi-verify" && argc >= 4) {
            std::string file_path = argv[2];
            std::string label = argv[3];
            std::vector<std::string> peers;
            if (argc > 4) {
                std::string peer_str = argv[4];
                size_t pos = 0;
                while ((pos = peer_str.find(',')) != std::string::npos) {
                    peers.push_back(peer_str.substr(0, pos));
                    peer_str.erase(0, pos + 1);
                }
                if (!peer_str.empty()) peers.push_back(peer_str);
            }
            return cli_multi_verify(file_path, label, peers);
        }

        // --propose-block <file> <label> <name> <id> <details> <peer1,peer2,...>
        if (cmd == "--propose-block" && argc >= 7) {
            std::string file_path = argv[2];
            std::string label = argv[3];
            std::string name = argv[4];
            std::string id = argv[5];
            std::string details = (argc > 6) ? argv[6] : "";
            std::vector<std::string> peers;
            if (argc > 7) {
                std::string peer_str = argv[7];
                size_t pos = 0;
                while ((pos = peer_str.find(',')) != std::string::npos) {
                    peers.push_back(peer_str.substr(0, pos));
                    peer_str.erase(0, pos + 1);
                }
                if (!peer_str.empty()) peers.push_back(peer_str);
            }
            return cli_propose_block(file_path, label, name, id, details, peers);
        }

        std::cout << "STATUS=ERROR\nMESSAGE=Invalid CLI usage" << std::endl;
        std::cout << "Usage:" << std::endl;
        std::cout << "  scdv_verifier --keygen [dir]            Generate ECDSA keypair" << std::endl;
        std::cout << "  scdv_verifier --multi-keygen [label...] Generate keys for consortium" << std::endl;
        std::cout << "  scdv_verifier --node [config]           Run as consensus node" << std::endl;
        std::cout << "  scdv_verifier --status                   Show chain/node status" << std::endl;
        std::cout << "  scdv_verifier --validators               List validators" << std::endl;
        std::cout << "  scdv_verifier --propose-block <file> <label> <name> <id> [details] [peer,...]" << std::endl;
        std::cout << "                         Propose multi-sig block" << std::endl;
        std::cout << "  scdv_verifier --multi-verify <file> <label> [peer,...]" << std::endl;
        std::cout << "                         Verify with multi-node consensus (2/3)" << std::endl;
        std::cout << "  scdv_verifier register <file> <label> <name> <id> [details]" << std::endl;
        std::cout << "  scdv_verifier verify <file> <label>" << std::endl;
        std::cout << "  scdv_verifier find <label>" << std::endl;
        std::cout << "  scdv_verifier find_student <name> <id>" << std::endl;
        std::cout << "  scdv_verifier validate" << std::endl;
        return 2;
    }

    // Interactive mode
    print_banner();

    // Check if node keys exist
    NodeConfig cfg = NodeConfig::load(NODE_CONFIG_FILE);
    if (cfg.priv_key.empty()) {
        std::cout << "[!] No node keys found. Run --keygen first.\n" << std::endl;
    } else {
        std::cout << "[OK] Node " << ECDSAUtils::pub_key_to_short_id(cfg.pub_key)
                  << " ready\n" << std::endl;
    }

    int choice;
    std::string input;

    while (true) {
        print_menu();
        std::getline(std::cin, input);

        try {
            choice = std::stoi(input);
        } catch (...) {
            std::cerr << "[!] Invalid input" << std::endl;
            continue;
        }

        switch (choice) {
            case 1: register_diploma(); break;
            case 2: verify_diploma();   break;
            case 3: view_blockchain();  break;
            case 4: validate_integrity(); break;
            case 5: cmd_node_status();  break;
            case 6: cmd_list_validators(); break;
            case 7:
                std::cout << "\n[+] Goodbye!" << std::endl;
                return 0;
            default:
                std::cerr << "[!] Invalid option" << std::endl;
        }
    }
}
