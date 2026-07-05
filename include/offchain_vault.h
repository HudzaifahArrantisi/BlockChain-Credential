#ifndef OFFCHAIN_VAULT_H
#define OFFCHAIN_VAULT_H

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class OffChainVault {
public:
    static std::string vault_dir();

    static bool ensure_vault_dir();

    static std::string save_details(const std::string& file_hash,
                                     const json& details);

    static json load_details(const std::string& file_hash);

    static bool verify_details(const std::string& file_hash,
                                const std::string& expected_hash);

    static std::string compute_details_hash(const json& details);
};

#endif
