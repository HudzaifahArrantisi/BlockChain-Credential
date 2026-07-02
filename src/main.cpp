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
                node.add_peer(peer, peer_pub);
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

        std::cout << "STATUS=ERROR\nMESSAGE=Invalid CLI usage" << std::endl;
        std::cout << "Usage:" << std::endl;
        std::cout << "  scdv_verifier --keygen [dir]         Generate ECDSA keypair" << std::endl;
        std::cout << "  scdv_verifier --node [config]        Run as consensus node" << std::endl;
        std::cout << "  scdv_verifier --status                Show chain/node status" << std::endl;
        std::cout << "  scdv_verifier --validators            List validators" << std::endl;
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
