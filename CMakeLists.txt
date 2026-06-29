cmake_minimum_required(VERSION 3.12)
project(SecureChainDiplomaVerifier)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenSSL
find_package(OpenSSL REQUIRED)
if(NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found.
Windows: Install from https://slproweb.com/products/Win32OpenSSL.html
Linux: sudo apt-get install libssl-dev
macOS: brew install openssl")
endif()

# Create data directory if not exists
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/data")

# Download nlohmann/json if not present
include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    QUIET
)
FetchContent_MakeAvailable(json)

# Source files
set(SOURCES
    src/main.cpp
    src/crypto_utils.cpp
    src/blockchain.cpp
    src/document_handler.cpp
)

# Create executable
add_executable(scdv_verifier ${SOURCES})

# Include directories
target_include_directories(scdv_verifier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENSSL_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(scdv_verifier
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# OpenSSL flags
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    message(STATUS "OpenSSL include: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL lib: ${OPENSSL_CRYPTO_LIBRARY}")
endif()

# Output message
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Executable output: ${CMAKE_BINARY_DIR}/scdv_verifier")
