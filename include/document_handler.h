#ifndef DOCUMENT_HANDLER_H
#define DOCUMENT_HANDLER_H

#include <string>
#include <vector>

class DocumentHandler {
public:
    static std::string compute_file_hash(const std::string& filepath);
    static bool file_exists(const std::string& filepath);
};

#endif // DOCUMENT_HANDLER_H
