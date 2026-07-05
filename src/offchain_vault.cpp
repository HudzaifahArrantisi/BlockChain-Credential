#include "../include/offchain_vault.h"
#include "../include/crypto_utils.h"
#include <iostream>
#include <fstream>
#include <filesystem>

std::string OffChainVault::vault_dir() {
    return "data/offchain_vault";
}

bool OffChainVault::ensure_vault_dir() {
    try {
        std::filesystem::create_directories(vault_dir());
        return true;
    } catch (...) {
        std::cerr << "[!] Failed to create offchain vault directory" << std::endl;
        return false;
    }
}

std::string OffChainVault::save_details(const std::string& file_hash,
                                         const json& details) {
    if (!ensure_vault_dir()) return "";

    std::string hash = compute_details_hash(details);
    std::string path = vault_dir() + "/" + file_hash + ".json";

    json out = details;
    out["_vault_hash"] = hash;
    out["_file_hash"] = file_hash;

    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[!] Cannot write off-chain vault: " << path << std::endl;
        return "";
    }
    f << out.dump(2);
    f.close();

    std::cout << "[VAULT] Saved off-chain data for " << file_hash.substr(0, 16)
              << "... -> " << path << std::endl;
    return hash;
}

json OffChainVault::load_details(const std::string& file_hash) {
    std::string path = vault_dir() + "/" + file_hash + ".json";
    std::ifstream f(path);
    if (!f.is_open()) return json();

    json j;
    f >> j;
    return j;
}

bool OffChainVault::verify_details(const std::string& file_hash,
                                    const std::string& expected_hash) {
    json details = load_details(file_hash);
    if (details.is_null() || details.empty()) return false;

    std::string actual_hash = compute_details_hash(details);
    return actual_hash == expected_hash;
}

std::string OffChainVault::compute_details_hash(const json& details) {
    std::string canonical;
    if (details.is_object()) {
        for (auto it = details.begin(); it != details.end(); ++it) {
            if (it.key() == "_vault_hash" || it.key() == "_file_hash") continue;
            canonical += it.key() + ":" + it.value().dump() + "\n";
        }
    } else {
        canonical = details.dump();
    }
    return CryptoUtils::sha256(canonical);
}
