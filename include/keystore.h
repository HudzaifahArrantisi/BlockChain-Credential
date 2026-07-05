#ifndef KEYSTORE_H
#define KEYSTORE_H

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Keypair {
    std::string label;
    std::string priv_key_hex;
    std::string pub_key_hex;
};

class Keystore {
public:
    static std::string keystore_dir();

    static bool ensure_keystore_dir();

    static bool save_keypair(const Keypair& kp);

    static Keypair load_keypair(const std::string& label);

    static std::string get_default_config_path(const std::string& label);

    static bool inject_into_config(const std::string& config_path,
                                    const std::string& label);
};

#endif
