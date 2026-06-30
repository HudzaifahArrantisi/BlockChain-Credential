#include <iostream>
#include <string>
#include "include/blockchain.h"
#include "include/document_handler.h"
#include "include/crypto_utils.h"

const std::string BLOCKCHAIN_FILE = "data/blockchain.json";
const std::string MASTER_KEY = "SecureChain2024!Key@Campus";

void print_banner() {
    std::cout << "\n╔════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  SecureChain Diploma Verifier (SCDV)          ║" << std::endl;
    std::cout << "║  Blockchain-based Document Verification       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════╝\n" << std::endl;
}

void print_menu() {
    std::cout << "\n[1] Register Diploma (Admin)" << std::endl;
    std::cout << "[2] Verify Diploma (User)" << std::endl;
    std::cout << "[3] View Blockchain" << std::endl;
    std::cout << "[4] Validate Chain Integrity" << std::endl;
    std::cout << "[5] Exit" << std::endl;
    std::cout << "\nSelect option: ";
}

void register_diploma() {
    StrictBlockchain blockchain(MASTER_KEY);
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
    StrictBlockchain blockchain(MASTER_KEY);
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
            std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
            std::cout << "║  ✓ VERIFIED ALUMNI - DIPLOMA AUTHENTIC ║" << std::endl;
            std::cout << "║  Double-Lock Passed:                   ║" << std::endl;
            std::cout << "║  [1] File Integrity: SHA-256 ✓         ║" << std::endl;
            std::cout << "║  [2] Ownership: Unique Label ✓         ║" << std::endl;
            std::cout << "╚════════════════════════════════════════╝" << std::endl;
        } else {
            std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
            std::cout << "║  ✗ VERIFICATION FAILED                 ║" << std::endl;
            std::cout << "║  Reason:                               ║" << std::endl;
            std::cout << "║  - File tampered OR                    ║" << std::endl;
            std::cout << "║  - Unique Label incorrect OR           ║" << std::endl;
            std::cout << "║  - Not registered in blockchain       ║" << std::endl;
            std::cout << "╚════════════════════════════════════════╝" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[!] ERROR: " << e.what() << std::endl;
    }
}

void view_blockchain() {
    StrictBlockchain blockchain(MASTER_KEY);
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    blockchain.print_chain();
}

void validate_integrity() {
    StrictBlockchain blockchain(MASTER_KEY);
    blockchain.load_from_file(BLOCKCHAIN_FILE);

    std::cout << "\n[INTEGRITY VALIDATION]" << std::endl;
    if (blockchain.validate_chain()) {
        std::cout << "✓ Blockchain is VALID and TAMPER-FREE" << std::endl;
    } else {
        std::cout << "✗ Blockchain validation FAILED - POSSIBLE TAMPERING!" << std::endl;
    }
}

// ── CLI mode (called by Python GUI) ──────────────────────────────────────
// Emits machine-parseable KEY=VALUE lines on stdout. One field per line so
// values containing spaces/colons (e.g. student names) are safe to parse.

int cli_register(const std::string& path, const std::string& label,
                 const std::string& name, const std::string& id,
                 const std::string& details = "") {
    StrictBlockchain blockchain(MASTER_KEY);
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
    StrictBlockchain blockchain(MASTER_KEY);
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
                    std::string decrypted_details = CryptoUtils::aes256_decrypt(b->encrypted_details, label);
                    std::cout << "\nDETAILS=" << decrypted_details;
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
    StrictBlockchain blockchain(MASTER_KEY);
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
                    std::string decrypted_details = CryptoUtils::aes256_decrypt(b->encrypted_details, label);
                    std::cout << "\nDETAILS=" << decrypted_details;
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
    StrictBlockchain blockchain(MASTER_KEY);
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
                    std::string decrypted = CryptoUtils::aes256_decrypt(b->encrypted_details, b->student_id);
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
    StrictBlockchain blockchain(MASTER_KEY);
    blockchain.load_from_file(BLOCKCHAIN_FILE);
    std::cout << "STATUS=" << (blockchain.validate_chain() ? "VALID" : "INVALID") << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    // Non-interactive CLI mode when arguments are supplied (used by the GUI).
    if (argc > 1) {
        std::string cmd = argv[1];
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
        return 2;
    }

    print_banner();

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
            case 1:
                register_diploma();
                break;
            case 2:
                verify_diploma();
                break;
            case 3:
                view_blockchain();
                break;
            case 4:
                validate_integrity();
                break;
            case 5:
                std::cout << "\n[+] Goodbye!" << std::endl;
                return 0;
            default:
                std::cerr << "[!] Invalid option" << std::endl;
        }
    }

    return 0;
}
