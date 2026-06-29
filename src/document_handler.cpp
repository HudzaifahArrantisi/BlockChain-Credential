#include "../include/document_handler.h"
#include "../include/crypto_utils.h"
#include <fstream>
#include <sys/stat.h>

bool DocumentHandler::file_exists(const std::string& filepath) {
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

std::string DocumentHandler::compute_file_hash(const std::string& filepath) {
    if (!file_exists(filepath)) {
        throw std::runtime_error("File not found: " + filepath);
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    std::vector<unsigned char> file_contents((std::istreambuf_iterator<char>(file)),
                                             std::istreambuf_iterator<char>());
    file.close();

    return CryptoUtils::sha256(file_contents);
}
