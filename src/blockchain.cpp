#include "../include/blockchain.h"
#include "../include/crypto_utils.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>

json Block::to_json() const {
    return json{
        {"index", index},
        {"previous_hash", previous_hash},
        {"file_hash", file_hash},
        {"encrypted_label", encrypted_label},
        {"student_name", student_name},
        {"student_id", student_id},
        {"timestamp", timestamp},
        {"block_hash", block_hash},
        {"encrypted_details", encrypted_details}
    };
}

Block Block::from_json(const json& j) {
    Block b;
    b.index = j["index"];
    b.previous_hash = j["previous_hash"];
    b.file_hash = j["file_hash"];
    b.encrypted_label = j["encrypted_label"];
    b.student_name = j["student_name"];
    b.student_id = j["student_id"];
    b.timestamp = j["timestamp"];
    b.block_hash = j["block_hash"];
    b.encrypted_details = j.value("encrypted_details", "");
    return b;
}

StrictBlockchain::StrictBlockchain(const std::string& key) : master_key(key) {}

std::string StrictBlockchain::get_last_block_hash() const {
    if (chain.empty()) {
        return "0";
    }
    return chain.back().block_hash;
}

std::string StrictBlockchain::calculate_block_hash(const Block& block) const {
    // Mencampur master_key (rahasia C++) ke dalam perhitungan hash agar blockchain
    // tidak bisa diregenerasi/dimanipulasi secara offline oleh orang luar.
    std::string data = block.index + block.previous_hash + block.file_hash +
                      block.encrypted_label + block.student_name +
                      block.student_id + block.timestamp +
                      block.encrypted_details + master_key;
    return CryptoUtils::sha256(data);
}

Block StrictBlockchain::create_block(const std::string& file_hash,
                                    const std::string& encrypted_label,
                                    const std::string& student_name,
                                    const std::string& student_id,
                                    const std::string& encrypted_details) {
    Block block;
    block.index = std::to_string(chain.size());
    block.previous_hash = get_last_block_hash();
    block.file_hash = file_hash;
    block.encrypted_label = encrypted_label;
    block.student_name = student_name;
    block.student_id = student_id;
    block.encrypted_details = encrypted_details;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    block.timestamp = ss.str();

    block.block_hash = calculate_block_hash(block);
    return block;
}

void StrictBlockchain::register_diploma(const std::string& file_hash,
                                       const std::string& unique_label,
                                       const std::string& student_name,
                                       const std::string& student_id,
                                       const std::string& details) {
    std::string encrypted_label = CryptoUtils::aes256_encrypt(unique_label, master_key);
    std::string encrypted_details = "";
    if (!details.empty()) {
        encrypted_details = CryptoUtils::aes256_encrypt(details, unique_label);
    }
    Block new_block = create_block(file_hash, encrypted_label, student_name, student_id, encrypted_details);
    chain.push_back(new_block);
    std::cout << "[+] Diploma registered: " << student_id << " (" << student_name << ")" << std::endl;
}

bool StrictBlockchain::verify_diploma(const std::string& file_hash,
                                     const std::string& unique_label) const {
    return verify_and_get(file_hash, unique_label) != nullptr;
}

const Block* StrictBlockchain::verify_and_get(const std::string& file_hash,
                                              const std::string& unique_label) const {
    for (const auto& block : chain) {
        try {
            std::string decrypted_label = CryptoUtils::aes256_decrypt(block.encrypted_label, master_key);
            if (decrypted_label == unique_label && block.file_hash == file_hash) {
                return &block;
            }
        } catch (...) {
            // Skip blocks that fail to decrypt (wrong key/corrupt); keep scanning.
            continue;
        }
    }
    return nullptr;
}

const Block* StrictBlockchain::find_by_label(const std::string& unique_label) const {
    for (const auto& block : chain) {
        try {
            std::string decrypted_label = CryptoUtils::aes256_decrypt(block.encrypted_label, master_key);
            if (decrypted_label == unique_label) {
                return &block;
            }
        } catch (...) {
            continue;
        }
    }
    return nullptr;
}

const Block* StrictBlockchain::find_by_name_and_id(const std::string& name, const std::string& id) const {
    for (const auto& block : chain) {
        if (block.student_name == name && block.student_id == id) {
            return &block;
        }
    }
    return nullptr;
}

bool StrictBlockchain::validate_chain() const {
    if (chain.empty()) {
        return true;
    }

    // Verify first block
    if (chain[0].previous_hash != "0") {
        std::cerr << "[!] Genesis block has invalid previous_hash" << std::endl;
        return false;
    }

    // Verify chain integrity
    for (size_t i = 0; i < chain.size(); i++) {
        std::string expected_hash = calculate_block_hash(chain[i]);
        if (chain[i].block_hash != expected_hash) {
            std::cerr << "[!] Block " << i << " hash mismatch (tampered)" << std::endl;
            return false;
        }

        if (i > 0 && chain[i].previous_hash != chain[i - 1].block_hash) {
            std::cerr << "[!] Block " << i << " previous_hash mismatch (chain broken)" << std::endl;
            return false;
        }
    }

    return true;
}

void StrictBlockchain::load_from_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[!] Cannot open blockchain file: " << filepath << std::endl;
        return;
    }

    json j;
    file >> j;
    file.close();

    chain.clear();
    if (j.contains("blocks")) {
        for (const auto& block_json : j["blocks"]) {
            chain.push_back(Block::from_json(block_json));
        }
    }

    if (!validate_chain()) {
        std::cerr << "[!] WARNING: Blockchain validation failed! Possible tampering detected." << std::endl;
    }
}

void StrictBlockchain::save_to_file(const std::string& filepath) const {
    json output;
    output["blocks"] = json::array();
    for (const auto& block : chain) {
        output["blocks"].push_back(block.to_json());
    }

    std::ofstream file(filepath);
    file << output.dump(4);
    file.close();
    std::cout << "[+] Blockchain saved to " << filepath << std::endl;
}

void StrictBlockchain::print_chain() const {
    std::cout << "\n=== BLOCKCHAIN STATE ===" << std::endl;
    std::cout << "Total blocks: " << chain.size() << std::endl;
    for (size_t i = 0; i < chain.size(); i++) {
        std::cout << "\n[Block " << i << "]" << std::endl;
        std::cout << "  Student: " << chain[i].student_name << " (" << chain[i].student_id << ")" << std::endl;
        std::cout << "  File Hash: " << chain[i].file_hash.substr(0, 16) << "..." << std::endl;
        std::cout << "  Block Hash: " << chain[i].block_hash.substr(0, 16) << "..." << std::endl;
    }
    std::cout << "\n========================\n" << std::endl;
}
