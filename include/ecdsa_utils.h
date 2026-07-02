#ifndef ECDSA_UTILS_H
#define ECDSA_UTILS_H

#include <string>
#include <vector>

struct ECDSAKeypair {
    std::string priv_key_hex;
    std::string pub_key_hex;
};

class ECDSAUtils {
public:
    static ECDSAKeypair generate_keypair();

    static std::string sign(const std::string& priv_key_hex, const std::string& data);
    static bool verify(const std::string& pub_key_hex, const std::string& data, const std::string& signature_hex);

    static std::string pub_key_to_short_id(const std::string& pub_key_hex);

    static std::string ecdh_aes_encrypt(const std::string& pub_key_hex, const std::string& plaintext);
    static std::string ecdh_aes_decrypt(const std::string& priv_key_hex, const std::string& ciphertext_hex);

private:
    static std::string bytes_to_hex(const unsigned char* data, size_t len);
    static std::vector<unsigned char> hex_to_bytes(const std::string& hex);
    static std::vector<unsigned char> sha256_bytes(const std::vector<unsigned char>& data);
};

#endif
