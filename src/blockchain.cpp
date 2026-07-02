#include "../include/blockchain.h"
#include "../include/crypto_utils.h"
#include "../include/ecdsa_utils.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <algorithm>

json Block::to_json() const {
    json j = {
        {"index", index},
        {"previous_hash", previous_hash},
        {"file_hash", file_hash},
        {"encrypted_label", encrypted_label},
        {"student_name", student_name},
        {"student_id", student_id},
        {"timestamp", timestamp},
        {"block_hash", block_hash},
        {"encrypted_details", encrypted_details},
        {"creator_id", creator_id},
        {"term", term}
    };
    if (!signature.empty()) j["signature"] = signature;
    return j;
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
    b.creator_id = j.value("creator_id", "");
    b.signature = j.value("signature", "");
    b.term = j.value("term", 0);
    return b;
}

StrictBlockchain::StrictBlockchain(const std::string& priv_key,
                                    const std::string& pub_key)
    : node_priv_key(priv_key), node_pub_key(pub_key) {}

void StrictBlockchain::set_node_keys(const std::string& priv_key,
                                      const std::string& pub_key) {
    node_priv_key = priv_key;
    node_pub_key = pub_key;
}

std::string StrictBlockchain::get_last_block_hash_internal() const {
    if (chain.empty()) return "0";
    return chain.back().block_hash;
}

std::string StrictBlockchain::calculate_block_hash(const Block& block) const {
    std::string data = block.index + block.previous_hash + block.file_hash +
                       block.encrypted_label + block.student_name +
                       block.student_id + block.timestamp +
                       block.encrypted_details + block.creator_id +
                       std::to_string(block.term);
    return CryptoUtils::sha256(data);
}

Block StrictBlockchain::create_block(const std::string& file_hash,
                                     const std::string& encrypted_label,
                                     const std::string& student_name,
                                     const std::string& student_id,
                                     const std::string& encrypted_details) {
    Block block;
    block.index = std::to_string(chain.size());
    block.previous_hash = get_last_block_hash_internal();
    block.file_hash = file_hash;
    block.encrypted_label = encrypted_label;
    block.student_name = student_name;
    block.student_id = student_id;
    block.encrypted_details = encrypted_details;
    block.creator_id = node_pub_key;
    block.term = 0;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    block.timestamp = ss.str();

    block.block_hash = calculate_block_hash(block);
    return block;
}

Block StrictBlockchain::create_unsigned_block(const std::string& file_hash,
                                               const std::string& encrypted_label,
                                               const std::string& student_name,
                                               const std::string& student_id,
                                               const std::string& encrypted_details,
                                               int64_t term) {
    Block block;
    block.index = std::to_string(chain.size());
    block.previous_hash = get_last_block_hash_internal();
    block.file_hash = file_hash;
    block.encrypted_label = encrypted_label;
    block.student_name = student_name;
    block.student_id = student_id;
    block.encrypted_details = encrypted_details;
    block.creator_id = node_pub_key;
    block.term = term;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    block.timestamp = ss.str();

    block.block_hash = calculate_block_hash(block);
    return block;
}

void StrictBlockchain::sign_block(Block& block) {
    std::string to_sign = block.block_hash;
    block.signature = ECDSAUtils::sign(node_priv_key, to_sign);
}

bool StrictBlockchain::append_signed_block(const Block& block) {
    if (!chain.empty() && block.previous_hash != chain.back().block_hash)
        return false;

    if (!block.signature.empty()) {
        if (!validate_block_signature(block))
            return false;
    }

    chain.push_back(block);
    return true;
}

bool StrictBlockchain::has_block(const std::string& file_hash) const {
    for (const auto& b : chain) {
        if (b.file_hash == file_hash) return true;
    }
    return false;
}

Block StrictBlockchain::get_last_block() const {
    if (chain.empty()) {
        Block g;
        g.index = "0";
        g.previous_hash = "0";
        g.timestamp = "GENESIS";
        g.block_hash = "0";
        return g;
    }
    return chain.back();
}

void StrictBlockchain::register_diploma(const std::string& file_hash,
                                        const std::string& unique_label,
                                        const std::string& student_name,
                                        const std::string& student_id,
                                        const std::string& details) {
    std::string encrypted_label;
    if (!node_pub_key.empty()) {
        encrypted_label = ECDSAUtils::ecdh_aes_encrypt(node_pub_key, unique_label);
    } else {
        encrypted_label = CryptoUtils::aes256_encrypt(unique_label, "SecureChain2024!Key@Campus");
    }

    std::string encrypted_details;
    if (!details.empty()) {
        if (!node_pub_key.empty()) {
            encrypted_details = ECDSAUtils::ecdh_aes_encrypt(node_pub_key, details);
        } else {
            encrypted_details = CryptoUtils::aes256_encrypt(details, unique_label);
        }
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
            std::string decrypted_label;
            if (!block.creator_id.empty()) {
                decrypted_label = ECDSAUtils::ecdh_aes_decrypt(node_priv_key, block.encrypted_label);
            } else {
                decrypted_label = CryptoUtils::aes256_decrypt(block.encrypted_label, "SecureChain2024!Key@Campus");
            }
            if (decrypted_label == unique_label && block.file_hash == file_hash)
                return &block;
        } catch (...) {
            continue;
        }
    }
    return nullptr;
}

const Block* StrictBlockchain::find_by_label(const std::string& unique_label) const {
    for (const auto& block : chain) {
        try {
            std::string decrypted_label;
            if (!block.creator_id.empty()) {
                decrypted_label = ECDSAUtils::ecdh_aes_decrypt(node_priv_key, block.encrypted_label);
            } else {
                decrypted_label = CryptoUtils::aes256_decrypt(block.encrypted_label, "SecureChain2024!Key@Campus");
            }
            if (decrypted_label == unique_label)
                return &block;
        } catch (...) {
            continue;
        }
    }
    return nullptr;
}

const Block* StrictBlockchain::find_by_name_and_id(const std::string& name,
                                                    const std::string& id) const {
    for (const auto& block : chain) {
        if (block.student_name == name && block.student_id == id)
            return &block;
    }
    return nullptr;
}

bool StrictBlockchain::validate_block_signature(const Block& block) const {
    if (block.signature.empty() || block.creator_id.empty())
        return false;

    for (const auto& v : validators) {
        if (v.pub_key_hex == block.creator_id) {
            return ECDSAUtils::verify(block.creator_id, block.block_hash, block.signature);
        }
    }
    return false;
}

bool StrictBlockchain::validate_chain() const {
    if (chain.empty()) return true;

    if (chain[0].previous_hash != "0") {
        std::cerr << "[!] Genesis block has invalid previous_hash" << std::endl;
        return false;
    }

    for (size_t i = 0; i < chain.size(); i++) {
        std::string expected_hash = calculate_block_hash(chain[i]);
        if (chain[i].block_hash != expected_hash) {
            std::cerr << "[!] Block " << i << " hash mismatch (tampered)" << std::endl;
            return false;
        }

        if (i > 0 && chain[i].previous_hash != chain[i - 1].block_hash) {
            std::cerr << "[!] Block " << i << " previous_hash mismatch" << std::endl;
            return false;
        }

        if (!chain[i].signature.empty() && !validate_block_signature(chain[i])) {
            std::cerr << "[!] Block " << i << " invalid signature" << std::endl;
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

    if (j.contains("validators")) {
        validators.clear();
        for (const auto& vj : j["validators"]) {
            Validator v;
            v.node_id = vj["node_id"];
            v.pub_key_hex = vj["pub_key_hex"];
            v.endpoint = vj.value("endpoint", "");
            v.is_active = vj.value("is_active", true);
            validators.push_back(v);
        }
    }

    if (!validate_chain()) {
        std::cerr << "[!] WARNING: Blockchain validation failed!" << std::endl;
    }
}

void StrictBlockchain::save_to_file(const std::string& filepath) const {
    json output;
    output["blocks"] = json::array();
    for (const auto& block : chain) {
        output["blocks"].push_back(block.to_json());
    }

    output["validators"] = json::array();
    for (const auto& v : validators) {
        output["validators"].push_back({
            {"node_id", v.node_id},
            {"pub_key_hex", v.pub_key_hex},
            {"endpoint", v.endpoint},
            {"is_active", v.is_active}
        });
    }

    std::ofstream file(filepath);
    file << output.dump(4);
    file.close();
    std::cout << "[+] Blockchain saved to " << filepath << std::endl;
}

std::string StrictBlockchain::get_chain_hash() const {
    if (chain.empty()) return "0";
    std::string all;
    for (const auto& b : chain) {
        all += b.block_hash;
    }
    return CryptoUtils::sha256(all);
}

void StrictBlockchain::add_validator(const Validator& v) {
    for (auto& existing : validators) {
        if (existing.node_id == v.node_id) {
            existing = v;
            return;
        }
    }
    validators.push_back(v);
}

void StrictBlockchain::remove_validator(const std::string& node_id) {
    validators.erase(
        std::remove_if(validators.begin(), validators.end(),
                       [&](const Validator& v) { return v.node_id == node_id; }),
        validators.end());
}

bool StrictBlockchain::has_validator(const std::string& node_id) const {
    for (const auto& v : validators) {
        if (v.node_id == node_id) return true;
    }
    return false;
}

void StrictBlockchain::print_chain() const {
    std::cout << "\n=== BLOCKCHAIN STATE ===" << std::endl;
    std::cout << "Total blocks: " << chain.size() << std::endl;
    std::cout << "Validators: " << validators.size() << std::endl;
    for (size_t i = 0; i < validators.size(); i++) {
        std::cout << "  [" << i << "] " << validators[i].node_id
                  << " @ " << validators[i].endpoint << std::endl;
    }
    for (size_t i = 0; i < chain.size(); i++) {
        std::cout << "\n[Block " << i << "]" << std::endl;
        std::cout << "  Student: " << chain[i].student_name
                  << " (" << chain[i].student_id << ")" << std::endl;
        std::cout << "  File Hash: " << chain[i].file_hash.substr(0, 16) << "..." << std::endl;
        std::cout << "  Block Hash: " << chain[i].block_hash.substr(0, 16) << "..." << std::endl;
        std::cout << "  Creator: " << chain[i].creator_id.substr(0, 16) << "..."
                  << " Term: " << chain[i].term << std::endl;
        if (!chain[i].signature.empty())
            std::cout << "  Signed: " << chain[i].signature.substr(0, 16) << "..." << std::endl;
    }
    std::cout << "\n========================\n" << std::endl;
}
