#include "../include/crypto_utils.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>

std::string CryptoUtils::bytes_to_hex(const unsigned char* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return ss.str();
}

std::vector<unsigned char> CryptoUtils::hex_to_bytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_string = hex.substr(i, 2);
        unsigned char byte = (unsigned char)strtol(byte_string.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

std::string CryptoUtils::sha256(const std::vector<unsigned char>& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create digest context");
    if (!EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) ||
        !EVP_DigestUpdate(ctx, data.data(), data.size()) ||
        !EVP_DigestFinal_ex(ctx, hash, &hash_len)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 computation failed");
    }
    EVP_MD_CTX_free(ctx);
    return bytes_to_hex(hash, hash_len);
}

std::string CryptoUtils::sha256(const std::string& data) {
    return sha256(std::vector<unsigned char>(data.begin(), data.end()));
}

std::string CryptoUtils::aes256_encrypt(const std::string& plaintext, const std::string& key) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    unsigned char iv[EVP_MAX_IV_LENGTH];
    std::vector<unsigned char> encrypted(plaintext.length() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int ciphertext_len = 0;

    // Generate random IV
    if (!RAND_bytes(iv, sizeof(iv))) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to generate random IV");
    }

    // Ensure key is 32 bytes (256 bits)
    unsigned char key_bytes[32];
    memset(key_bytes, 0, sizeof(key_bytes));
    size_t key_len = (key.length() < 32) ? key.length() : 32;
    memcpy(key_bytes, key.c_str(), key_len);

    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key_bytes, iv)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption init failed");
    }

    if (!EVP_EncryptUpdate(ctx, encrypted.data(), &len, (const unsigned char*)plaintext.data(), plaintext.length())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption update failed");
    }
    ciphertext_len = len;

    if (!EVP_EncryptFinal_ex(ctx, encrypted.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption final failed");
    }
    ciphertext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    // Return IV + ciphertext in hex format
    std::string result = bytes_to_hex(iv, EVP_MAX_IV_LENGTH) +
                        bytes_to_hex(encrypted.data(), ciphertext_len);
    return result;
}

std::string CryptoUtils::aes256_decrypt(const std::string& ciphertext_hex, const std::string& key) {
    // Extract IV (first 32 hex chars = 16 bytes)
    std::string iv_hex = ciphertext_hex.substr(0, 32);
    std::string encrypted_hex = ciphertext_hex.substr(32);

    std::vector<unsigned char> iv = hex_to_bytes(iv_hex);
    std::vector<unsigned char> encrypted = hex_to_bytes(encrypted_hex);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> decrypted(encrypted.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    // Ensure key is 32 bytes (256 bits)
    unsigned char key_bytes[32];
    memset(key_bytes, 0, sizeof(key_bytes));
    size_t key_len = (key.length() < 32) ? key.length() : 32;
    memcpy(key_bytes, key.c_str(), key_len);

    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key_bytes, iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption init failed");
    }

    if (!EVP_DecryptUpdate(ctx, decrypted.data(), &len, encrypted.data(), encrypted.size())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption update failed");
    }
    plaintext_len = len;

    if (!EVP_DecryptFinal_ex(ctx, decrypted.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption final failed");
    }
    plaintext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)decrypted.data(), plaintext_len);
}
