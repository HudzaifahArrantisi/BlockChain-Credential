#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <string>
#include <vector>
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

    json to_json() const;
    static Block from_json(const json& j);
};

class StrictBlockchain {
public:
    StrictBlockchain(const std::string& master_key);

    void register_diploma(const std::string& file_hash,
                         const std::string& unique_label,
                         const std::string& student_name,
                         const std::string& student_id);

    bool verify_diploma(const std::string& file_hash,
                       const std::string& unique_label) const;

    // Returns pointer to matching block (with student data) or nullptr if not verified
    const Block* verify_and_get(const std::string& file_hash,
                                const std::string& unique_label) const;

    bool validate_chain() const;
    void load_from_file(const std::string& filepath);
    void save_to_file(const std::string& filepath) const;
    void print_chain() const;

private:
    std::vector<Block> chain;
    std::string master_key;

    std::string calculate_block_hash(const Block& block) const;
    std::string get_last_block_hash() const;
    Block create_block(const std::string& file_hash,
                      const std::string& encrypted_label,
                      const std::string& student_name,
                      const std::string& student_id);
};

#endif // BLOCKCHAIN_H
