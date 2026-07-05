#include "../include/ecdsa_utils.h"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/bn.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

static const int CURVE_NID = NID_secp256k1;

std::string ECDSAUtils::bytes_to_hex(const unsigned char* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return ss.str();
}

std::vector<unsigned char> ECDSAUtils::hex_to_bytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        bytes.push_back((unsigned char)strtol(byte_str.c_str(), nullptr, 16));
    }
    return bytes;
}

std::vector<unsigned char> ECDSAUtils::sha256_bytes(const std::vector<unsigned char>& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) ||
        !EVP_DigestUpdate(ctx, data.data(), data.size()) ||
        !EVP_DigestFinal_ex(ctx, hash, &hash_len)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 failed");
    }
    EVP_MD_CTX_free(ctx);
    return std::vector<unsigned char>(hash, hash + hash_len);
}

ECDSAKeypair ECDSAUtils::generate_keypair() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) throw std::runtime_error("Failed to create EC keygen context");

    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, CURVE_NID) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("Failed to init EC keygen");
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("Failed to generate EC keypair");
    }
    EVP_PKEY_CTX_free(ctx);

    EC_KEY* ec_key = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec_key) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Failed to get EC_KEY");
    }

    const BIGNUM* priv_bn = EC_KEY_get0_private_key(ec_key);
    if (!priv_bn) {
        EC_KEY_free(ec_key);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Failed to get private key");
    }

    char* priv_hex = BN_bn2hex(priv_bn);
    std::string priv_hex_str(priv_hex);
    OPENSSL_free(priv_hex);

    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    const EC_POINT* pub_pt = EC_KEY_get0_public_key(ec_key);
    char* pub_hex = EC_POINT_point2hex(group, pub_pt, POINT_CONVERSION_UNCOMPRESSED, nullptr);
    std::string pub_hex_str(pub_hex);
    OPENSSL_free(pub_hex);

    EC_KEY_free(ec_key);
    EVP_PKEY_free(pkey);

    return {priv_hex_str, pub_hex_str};
}

std::string ECDSAUtils::sign(const std::string& priv_key_hex, const std::string& data) {
    BIGNUM* priv_bn = BN_new();
    BN_hex2bn(&priv_bn, priv_key_hex.c_str());

    EC_KEY* ec_key = EC_KEY_new_by_curve_name(CURVE_NID);
    EC_KEY_set_private_key(ec_key, priv_bn);

    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    EC_POINT* pub_pt = EC_POINT_new(group);
    EC_POINT_mul(group, pub_pt, priv_bn, nullptr, nullptr, nullptr);
    BN_free(priv_bn);
    EC_KEY_set_public_key(ec_key, pub_pt);
    EC_POINT_free(pub_pt);

    std::vector<unsigned char> hash = sha256_bytes(
        std::vector<unsigned char>(data.begin(), data.end()));

    unsigned char sig_buf[EVP_MAX_MD_SIZE];
    unsigned int sig_len = 0;

    if (!ECDSA_sign(0, hash.data(), hash.size(), sig_buf, &sig_len, ec_key)) {
        EC_KEY_free(ec_key);
        throw std::runtime_error("ECDSA sign failed");
    }

    EC_KEY_free(ec_key);
    return bytes_to_hex(sig_buf, sig_len);
}

bool ECDSAUtils::verify(const std::string& pub_key_hex, const std::string& data,
                        const std::string& signature_hex) {
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(CURVE_NID);

    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    EC_POINT* pub_pt = EC_POINT_new(group);
    if (!EC_POINT_hex2point(group, pub_key_hex.c_str(), pub_pt, nullptr)) {
        EC_POINT_free(pub_pt);
        EC_KEY_free(ec_key);
        return false;
    }
    EC_KEY_set_public_key(ec_key, pub_pt);
    EC_POINT_free(pub_pt);

    std::vector<unsigned char> hash = sha256_bytes(
        std::vector<unsigned char>(data.begin(), data.end()));
    std::vector<unsigned char> sig_bytes = hex_to_bytes(signature_hex);

    int ret = ECDSA_verify(0, hash.data(), hash.size(),
                           sig_bytes.data(), sig_bytes.size(), ec_key);
    EC_KEY_free(ec_key);

    return ret == 1;
}

std::string ECDSAUtils::pub_key_to_short_id(const std::string& pub_key_hex) {
    std::vector<unsigned char> bytes = hex_to_bytes(pub_key_hex);
    std::vector<unsigned char> hash = sha256_bytes(bytes);
    return bytes_to_hex(hash.data(), 8);
}

std::string ECDSAUtils::ecdh_aes_encrypt(const std::string& pub_key_hex,
                                           const std::string& plaintext) {
    EC_KEY* recipient_key = EC_KEY_new_by_curve_name(CURVE_NID);
    const EC_GROUP* group = EC_KEY_get0_group(recipient_key);
    EC_POINT* recipient_pub = EC_POINT_new(group);
    if (!EC_POINT_hex2point(group, pub_key_hex.c_str(), recipient_pub, nullptr)) {
        EC_POINT_free(recipient_pub);
        EC_KEY_free(recipient_key);
        throw std::runtime_error("Invalid recipient public key hex: " + pub_key_hex.substr(0, 32));
    }

    EC_KEY* ephemeral_key = EC_KEY_new_by_curve_name(CURVE_NID);
    EC_KEY_generate_key(ephemeral_key);

    const EC_POINT* ephemeral_pub = EC_KEY_get0_public_key(ephemeral_key);
    char* ephemeral_pub_hex = EC_POINT_point2hex(group, ephemeral_pub,
                                                  POINT_CONVERSION_UNCOMPRESSED, nullptr);
    if (!ephemeral_pub_hex) {
        EC_POINT_free(recipient_pub);
        EC_KEY_free(ephemeral_key);
        EC_KEY_free(recipient_key);
        throw std::runtime_error("Failed to convert ephemeral pub key to hex");
    }
    std::string ephemeral_pub_hex_str(ephemeral_pub_hex);
    OPENSSL_free(ephemeral_pub_hex);

    const BIGNUM* ephemeral_priv = EC_KEY_get0_private_key(ephemeral_key);

    int field_size = EC_GROUP_get_degree(group);
    int coord_len = (field_size + 7) / 8;

    EC_POINT* shared_pt = EC_POINT_new(group);
    if (EC_POINT_mul(group, shared_pt, nullptr, recipient_pub, ephemeral_priv, nullptr) <= 0) {
        EC_POINT_free(shared_pt);
        EC_POINT_free(recipient_pub);
        EC_KEY_free(ephemeral_key);
        EC_KEY_free(recipient_key);
        throw std::runtime_error("EC_POINT_mul failed");
    }
    EC_POINT_free(recipient_pub);

    // Extract x-coordinate BEFORE freeing keys (group is owned by recipient_key)
    unsigned char point_buf[65];
    int point_len = EC_POINT_point2oct(group, shared_pt, POINT_CONVERSION_UNCOMPRESSED,
                                        point_buf, sizeof(point_buf), nullptr);
    EC_POINT_free(shared_pt);
    if (point_len <= 0) {
        EC_KEY_free(ephemeral_key);
        EC_KEY_free(recipient_key);
        throw std::runtime_error("Failed to serialize shared point");
    }

    EC_KEY_free(ephemeral_key);
    EC_KEY_free(recipient_key);

    // x-coordinate is bytes 1..32 (uncompressed: 04 | x(32) | y(32))
    std::vector<unsigned char> x_coord(point_buf + 1, point_buf + 1 + coord_len);
    std::vector<unsigned char> aes_key = sha256_bytes(x_coord);

    unsigned char iv[16];
    RAND_bytes(iv, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> encrypted(plaintext.length() + EVP_MAX_BLOCK_LENGTH);
    int len = 0, ciphertext_len = 0;

    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aes_key.data(), iv)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("ECIES encrypt init failed");
    }
    if (!EVP_EncryptUpdate(ctx, encrypted.data(), &len,
                           (const unsigned char*)plaintext.data(), plaintext.length())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("ECIES encrypt update failed");
    }
    ciphertext_len = len;
    if (!EVP_EncryptFinal_ex(ctx, encrypted.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("ECIES encrypt final failed");
    }
    ciphertext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    std::string result = ephemeral_pub_hex_str + "|" +
                         bytes_to_hex(iv, 16) + "|" +
                         bytes_to_hex(encrypted.data(), ciphertext_len);
    return result;
}

std::string ECDSAUtils::ecdh_aes_decrypt(const std::string& priv_key_hex,
                                          const std::string& ciphertext_hex) {
    size_t first_pipe = ciphertext_hex.find('|');
    size_t second_pipe = ciphertext_hex.find('|', first_pipe + 1);
    if (first_pipe == std::string::npos || second_pipe == std::string::npos)
        throw std::runtime_error("Invalid ECIES ciphertext format");

    std::string ephemeral_pub_hex = ciphertext_hex.substr(0, first_pipe);
    std::string iv_hex = ciphertext_hex.substr(first_pipe + 1, second_pipe - first_pipe - 1);
    std::string encrypted_hex = ciphertext_hex.substr(second_pipe + 1);

    BIGNUM* priv_bn = BN_new();
    BN_hex2bn(&priv_bn, priv_key_hex.c_str());

    EC_KEY* recipient_key = EC_KEY_new_by_curve_name(CURVE_NID);
    EC_KEY_set_private_key(recipient_key, priv_bn);
    BN_free(priv_bn);

    const EC_GROUP* group = EC_KEY_get0_group(recipient_key);
    EC_POINT* ephemeral_pt = EC_POINT_new(group);
    if (!EC_POINT_hex2point(group, ephemeral_pub_hex.c_str(), ephemeral_pt, nullptr)) {
        EC_POINT_free(ephemeral_pt);
        EC_KEY_free(recipient_key);
        throw std::runtime_error("Invalid ephemeral public key");
    }

    int field_size = EC_GROUP_get_degree(group);
    int coord_len = (field_size + 7) / 8;

    EC_POINT* shared_pt = EC_POINT_new(group);
    if (EC_POINT_mul(group, shared_pt, nullptr, ephemeral_pt, EC_KEY_get0_private_key(recipient_key), nullptr) <= 0) {
        EC_POINT_free(shared_pt);
        EC_POINT_free(ephemeral_pt);
        EC_KEY_free(recipient_key);
        throw std::runtime_error("EC_POINT_mul failed in decrypt");
    }
    EC_POINT_free(ephemeral_pt);

    // Extract x-coordinate BEFORE freeing recipient_key (group is owned by it)
    unsigned char point_buf[65];
    int point_len = EC_POINT_point2oct(group, shared_pt, POINT_CONVERSION_UNCOMPRESSED,
                                        point_buf, sizeof(point_buf), nullptr);
    EC_POINT_free(shared_pt);
    if (point_len <= 0) {
        EC_KEY_free(recipient_key);
        throw std::runtime_error("Failed to serialize shared point in decrypt");
    }

    EC_KEY_free(recipient_key);

    std::vector<unsigned char> x_coord(point_buf + 1, point_buf + 1 + coord_len);
    std::vector<unsigned char> aes_key = sha256_bytes(x_coord);
    std::vector<unsigned char> iv = hex_to_bytes(iv_hex);
    std::vector<unsigned char> encrypted = hex_to_bytes(encrypted_hex);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> decrypted(encrypted.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0, plaintext_len = 0;

    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, aes_key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("ECIES decrypt init failed");
    }
    if (!EVP_DecryptUpdate(ctx, decrypted.data(), &len, encrypted.data(), encrypted.size())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("ECIES decrypt update failed");
    }
    plaintext_len = len;
    if (!EVP_DecryptFinal_ex(ctx, decrypted.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("ECIES decrypt final failed");
    }
    plaintext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)decrypted.data(), plaintext_len);
}
