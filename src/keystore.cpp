#include "../include/keystore.h"
#include <iostream>
#include <fstream>
#include <filesystem>

std::string Keystore::keystore_dir() {
    return ".keystore";
}

bool Keystore::ensure_keystore_dir() {
    try {
        std::filesystem::create_directories(keystore_dir());
        return true;
    } catch (...) {
        std::cerr << "[!] Failed to create keystore directory" << std::endl;
        return false;
    }
}

bool Keystore::save_keypair(const Keypair& kp) {
    if (!ensure_keystore_dir()) return false;

    std::string path = keystore_dir() + "/" + kp.label + ".key";
    json j;
    j["label"] = kp.label;
    j["priv_key"] = kp.priv_key_hex;
    j["pub_key"] = kp.pub_key_hex;
    j["created"] = "SCDV-v2";

    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[!] Cannot write keystore: " << path << std::endl;
        return false;
    }
    f << j.dump(2);
    f.close();

    std::cout << "[KEYSTORE] Saved keypair '" << kp.label << "' -> " << path << std::endl;
    return true;
}

Keypair Keystore::load_keypair(const std::string& label) {
    Keypair kp;
    kp.label = label;

    std::string path = keystore_dir() + "/" + label + ".key";
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[KEYSTORE] Keypair not found: " << path << std::endl;
        return kp;
    }

    json j;
    f >> j;
    kp.priv_key_hex = j.value("priv_key", "");
    kp.pub_key_hex = j.value("pub_key", "");
    return kp;
}

std::string Keystore::get_default_config_path(const std::string& label) {
    return "data/node_" + label + "/node_config.json";
}

bool Keystore::inject_into_config(const std::string& config_path,
                                   const std::string& label) {
    Keypair kp = load_keypair(label);
    if (kp.priv_key_hex.empty() || kp.pub_key_hex.empty()) {
        std::cerr << "[KEYSTORE] Cannot inject: keypair '" << label << "' not found" << std::endl;
        return false;
    }

    std::ifstream fi(config_path);
    if (!fi.is_open()) {
        std::cerr << "[KEYSTORE] Config not found: " << config_path << std::endl;
        return false;
    }
    json cfg;
    fi >> cfg;
    fi.close();

    cfg["priv_key"] = kp.priv_key_hex;
    cfg["pub_key"] = kp.pub_key_hex;

    std::ofstream fo(config_path);
    fo << cfg.dump(2);
    fo.close();

    std::cout << "[KEYSTORE] Injected keys into " << config_path << std::endl;
    return true;
}
