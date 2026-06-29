#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <string>
#include <vector>

class CryptoUtils {
public:
    static std::string sha256(const std::vector<unsigned char>& data);
    static std::string sha256(const std::string& data);

    static std::string aes256_encrypt(const std::string& plaintext, const std::string& key);
    static std::string aes256_decrypt(const std::string& ciphertext, const std::string& key);

private:
    static std::string bytes_to_hex(const unsigned char* data, size_t len);
    static std::vector<unsigned char> hex_to_bytes(const std::string& hex);
};

#endif // CRYPTO_UTILS_H
