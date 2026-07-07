#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Block {
    std::string index;
    std::string previous_hash;
    std::string file_hash;
    std::string encrypted_label;
    std::string student_name;
    std::string student_id;
    std::string timestamp;
    std::string block_hash;
    std::string encrypted_details;  // kept for backward compat, empty on new blocks
    std::string details_hash;        // hash of off-chain vault data (new)

    std::string creator_id;
    std::string signature;           // proposer's signature
    std::vector<std::string> validator_sigs;  // multi-sig from other validators
    int64_t term = 0;

    json to_json() const;
    static Block from_json(const json& j);
};

struct Validator {
    std::string node_id;
    std::string pub_key_hex;
    std::string endpoint;
    bool is_active = true;
};

enum class NodeRole {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

class StrictBlockchain {
public:
    StrictBlockchain(const std::string& node_priv_key = "",
                     const std::string& node_pub_key = "");

    void set_node_keys(const std::string& priv_key, const std::string& pub_key);

    void register_diploma(const std::string& file_hash,
                         const std::string& unique_label,
                         const std::string& student_name,
                         const std::string& student_id,
                         const std::string& details = "");

    bool verify_diploma(const std::string& file_hash,
                       const std::string& unique_label) const;

    const Block* verify_and_get(const std::string& file_hash,
                                const std::string& unique_label) const;
    const Block* find_by_label(const std::string& unique_label) const;
    const Block* find_by_name_and_id(const std::string& name, const std::string& id) const;

    bool validate_chain() const;
    bool validate_block_signature(const Block& block) const;

    void load_from_file(const std::string& filepath);
    void save_to_file(const std::string& filepath) const;
    void print_chain() const;

    std::vector<Block> get_all_blocks() const { return chain; }
    size_t chain_length() const { return chain.size(); }
    std::string get_chain_hash() const;

    // Consensus hooks
    Block create_unsigned_block(const std::string& file_hash,
                                const std::string& encrypted_label,
                                const std::string& student_name,
                                const std::string& student_id,
                                const std::string& encrypted_details = "",
                                int64_t term = 0);
    void sign_block(Block& block);
    bool append_signed_block(const Block& block);
    bool has_block(const std::string& file_hash) const;
    Block get_last_block() const;
    std::string get_last_block_hash() const;

    Block prepare_block_proposal(const std::string& file_hash,
                                  const std::string& encrypted_label,
                                  const std::string& student_name,
                                  const std::string& student_id,
                                  const std::string& details = "");
    bool append_multi_sig_block(const Block& block);
    json get_offchain_details(const std::string& file_hash) const;

    std::string get_node_priv_key() const { return node_priv_key; }
    std::string get_node_pub_key() const { return node_pub_key; }

    std::string calculate_block_hash(const Block& block) const;

    void add_validator(const Validator& v);
    void remove_validator(const std::string& node_id);
    std::vector<Validator> get_validators() const { return validators; }
    bool has_validator(const std::string& node_id) const;

private:
    std::vector<Block> chain;
    std::vector<Validator> validators;

    std::string node_priv_key;
    std::string node_pub_key;

    std::string get_last_block_hash_internal() const;
    Block create_block(const std::string& file_hash,
                       const std::string& encrypted_label,
                       const std::string& student_name,
                       const std::string& student_id,
                       const std::string& encrypted_details = "");
};

#endif
