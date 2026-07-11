#include "../include/node.h"
#include "../include/ecdsa_utils.h"
#include "../include/offchain_vault.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <random>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

static const int SERVER_PORT = 0;
static const int RAFT_HEARTBEAT_MS = 400;
static const int RAFT_ELECTION_MIN_MS = 4000;
static const int RAFT_ELECTION_MAX_MS = 10000;
static const int RAFT_STARTUP_STABILIZE_MS = 4000;
static const int HTTP_BUFFER_SIZE = 65536;

// ── Priority failover tuning ─────────────────────────────────────────────
// The election timeout doubles as the leader-death detector. The designated
// successor uses the SHORTEST timeout (SUCCESSOR_BASE_MS) so it notices the
// dead leader first and wins; each subsequent node in the ring waits an extra
// SUCCESSOR_STEP_MS. All are >> the 400ms heartbeat, so a live leader never
// triggers a false election. FRESH_START_* is used before any leader exists.
static const int SUCCESSOR_BASE_MS = 3000;
static const int SUCCESSOR_STEP_MS = 1000;
static const int FRESH_START_MIN_MS = 3000;
static const int FRESH_START_MAX_MS = 6000;

static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

#ifdef _WIN32
class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockGuard() { WSACleanup(); }
};
static WinsockGuard __winsock_guard;
#endif

// ── Helpers ────────────────────────────────────────────────────────────────

std::vector<std::string> ConsensusNode::split_string(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
        parts.push_back(item);
    return parts;
}

std::string ConsensusNode::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

static std::string url_encode(const std::string& s) {
    // Minimal URL encoding (just for path/query safety)
    std::string encoded;
    for (char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            encoded += c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            encoded += buf;
        }
    }
    return encoded;
}

// ── Socket helpers ─────────────────────────────────────────────────────────

static bool create_server_socket(const std::string& addr, SOCKET& out_sock,
                                  int& out_port) {
    std::string host = addr;
    int port = 0;
    size_t colon = addr.find(':');
    if (colon != std::string::npos) {
        host = addr.substr(0, colon);
        port = std::stoi(addr.substr(colon + 1));
    }

    out_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out_sock == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(out_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = host.empty() ? INADDR_ANY : inet_addr(host.c_str());
    sin.sin_port = htons(port);

    if (bind(out_sock, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        closesocket(out_sock);
        return false;
    }

    if (port == 0) {
        socklen_t len = sizeof(sin);
        getsockname(out_sock, (sockaddr*)&sin, &len);
        out_port = ntohs(sin.sin_port);
    } else {
        out_port = port;
    }

    listen(out_sock, SOMAXCONN);
    return true;
}

static std::string recv_all(SOCKET sock) {
    std::string data;
    char buf[4096];
    int received;
    // Disable Nagle for responsiveness
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
    // Set receive timeout so recv doesn't hang forever
    timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    do {
        received = recv(sock, buf, sizeof(buf) - 1, 0);
        if (received > 0) {
            buf[received] = '\0';
            data += buf;
        } else if (received == 0) {
            break;
        } else {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT && !data.empty()) break;
            break;
        }
    } while (received == sizeof(buf) - 1);

    return data;
}

static bool send_all(SOCKET sock, const std::string& data) {
    int total_sent = 0;
    while (total_sent < (int)data.length()) {
        int sent = send(sock, data.c_str() + total_sent,
                        data.length() - total_sent, 0);
        if (sent == SOCKET_ERROR) return false;
        total_sent += sent;
    }
    return true;
}

static std::pair<std::string, std::string> parse_http_request(const std::string& raw) {
    std::string method, path, body;
    size_t first_space = raw.find(' ');
    if (first_space == std::string::npos) return {"", ""};
    method = raw.substr(0, first_space);

    size_t second_space = raw.find(' ', first_space + 1);
    if (second_space == std::string::npos) return {"", ""};
    path = raw.substr(first_space + 1, second_space - first_space - 1);

    size_t body_start = raw.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        body = raw.substr(body_start + 4);
    }

    return {method, path};
}

// ── HTTP static methods ────────────────────────────────────────────────────

std::string ConsensusNode::http_get(const std::string& url, const std::string& body) {
    return http_post(url, body);
}

// Non-blocking connect with timeout
static bool connect_with_timeout(SOCKET sock, const sockaddr_in& sin, int timeout_sec) {
#ifdef _WIN32
    u_long nonblock = 1;
    ioctlsocket(sock, FIONBIO, &nonblock);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
    connect(sock, (sockaddr*)&sin, sizeof(sin));
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);
    timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    int rc = select((int)(sock + 1), nullptr, &fdset, nullptr, &tv);
#ifdef _WIN32
    u_long block = 0;
    ioctlsocket(sock, FIONBIO, &block);
#else
    fcntl(sock, F_SETFL, flags);
#endif
    if (rc <= 0) {
        closesocket(sock);
        return false;
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
    return so_error == 0;
}

std::string ConsensusNode::http_post(const std::string& url, const std::string& body) {
    std::string host, path = "/";
    int port = 80;

    std::string u = url;
    if (u.find("http://") == 0) u = u.substr(7);
    else if (u.find("https://") == 0) u = u.substr(8);

    size_t colon = u.find(':');
    size_t slash = u.find('/');
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
        host = u.substr(0, colon);
        port = std::stoi(u.substr(colon + 1, slash - colon - 1));
        if (slash != std::string::npos) path = u.substr(slash);
    } else if (slash != std::string::npos) {
        host = u.substr(0, slash);
        path = u.substr(slash);
    } else {
        host = u;
        path = "/";
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return "";

    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host.c_str());
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        closesocket(sock);
        return "";
    }

    if (!connect_with_timeout(sock, sin, 3)) {
        return "";
    }

    std::stringstream req;
    if (body.empty()) {
        req << "GET " << path << " HTTP/1.0\r\n"
            << "Host: " << host << "\r\n"
            << "Connection: close\r\n\r\n";
    } else {
        req << "POST " << path << " HTTP/1.0\r\n"
            << "Host: " << host << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.length() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;
    }

    send_all(sock, req.str());
    std::string response = recv_all(sock);
    closesocket(sock);

    size_t hdr_end = response.find("\r\n\r\n");
    if (hdr_end != std::string::npos) {
        return response.substr(hdr_end + 4);
    }
    return response;
}

// ── Constructor / Destructor ───────────────────────────────────────────────

ConsensusNode::ConsensusNode(const std::string& addr,
                              const std::string& dir,
                              const std::string& priv_key,
                              const std::string& pub_key)
    : listen_addr(addr)
    , data_dir(dir)
    , blockchain(priv_key, pub_key)
    , node_id(ECDSAUtils::pub_key_to_short_id(pub_key))
{
    blockchain_file = data_dir + "/blockchain.json";
    last_heartbeat_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

ConsensusNode::~ConsensusNode() {
    stop();
}

// ── Peer Management ────────────────────────────────────────────────────────

void ConsensusNode::add_peer(const std::string& endpoint, const std::string& pub_key_hex,
                              const std::string& peer_node_id) {
    std::lock_guard<std::mutex> lock(peers_mutex);
    for (auto& p : peers) {
        if (p.endpoint == endpoint) {
            if (!pub_key_hex.empty()) p.pub_key_hex = pub_key_hex;
            if (!peer_node_id.empty()) p.node_id = peer_node_id;
            return;
        }
    }
    Peer p;
    p.node_id = peer_node_id;
    p.endpoint = endpoint;
    p.pub_key_hex = pub_key_hex;
    p.is_active = false;
    peers.push_back(p);
}

void ConsensusNode::remove_peer(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(peers_mutex);
    peers.erase(std::remove_if(peers.begin(), peers.end(),
                [&](const Peer& p) { return p.endpoint == endpoint; }),
                peers.end());
}

std::vector<Peer> ConsensusNode::get_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex);
    return peers;
}

void ConsensusNode::set_known_peers(const std::vector<std::string>& endpoints) {
    std::lock_guard<std::mutex> lock(peers_mutex);
    for (const auto& ep : endpoints) {
        bool found = false;
        for (auto& p : peers) {
            if (p.endpoint == ep) { found = true; break; }
        }
        if (!found) {
            Peer p;
            p.endpoint = ep;
            p.is_active = false;
            peers.push_back(p);
        }
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void ConsensusNode::start() {
    if (running.load()) return;
    running.store(true);

    blockchain.load_from_file(blockchain_file);

    server_thread = std::thread(&ConsensusNode::run_server, this);
    consensus_thread = std::thread(&ConsensusNode::run_consensus_loop, this);

    std::cout << "[NODE " << node_id << "] Started at " << listen_addr
              << " | Role: " << get_role_str() << std::endl;
}

void ConsensusNode::stop() {
    if (!running.load()) return;
    running.store(false);

    if (server_thread.joinable()) server_thread.join();
    if (consensus_thread.joinable()) consensus_thread.join();

    blockchain.save_to_file(blockchain_file);
    std::cout << "[NODE " << node_id << "] Stopped." << std::endl;
}

std::string ConsensusNode::get_role_str() const {
    switch (role.load()) {
        case NodeRole::FOLLOWER:  return "FOLLOWER";
        case NodeRole::CANDIDATE: return "CANDIDATE";
        case NodeRole::LEADER:    return "LEADER";
    }
    return "UNKNOWN";
}

// ── HTTP Server ────────────────────────────────────────────────────────────

void ConsensusNode::run_server() {
    SOCKET server_sock;
    int actual_port;
    if (!create_server_socket(listen_addr, server_sock, actual_port)) {
        std::cerr << "[NODE] Failed to create server socket on " << listen_addr << std::endl;
        running.store(false);
        return;
    }

    // Update listen_addr with actual port
    listen_addr = "0.0.0.0:" + std::to_string(actual_port);

    // Single-threaded accept-and-handle loop.
    // Election timeout (4-10s) provides ample buffer so a slow API call
    // won't cause a missed heartbeat.
    fd_set read_fds;
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;

    while (running.load()) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        int ret = select(0, &read_fds, nullptr, nullptr, &tv);
        if (ret == SOCKET_ERROR) continue;
        if (ret == 0) continue;

        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client = accept(server_sock, (sockaddr*)&client_addr, &client_len);
        if (client == INVALID_SOCKET) continue;

        std::string raw = recv_all(client);
        auto [method, path] = parse_http_request(raw);
        std::string body;
        size_t bb = raw.find("\r\n\r\n");
        if (bb != std::string::npos) body = raw.substr(bb + 4);

        std::string response_body;
        try {
            response_body = handle_request(method, path, body);
        } catch (const std::exception& e) {
            json err;
            err["status"] = "ERROR";
            err["message"] = e.what();
            response_body = err.dump();
        } catch (...) {
            json err;
            err["status"] = "ERROR";
            err["message"] = "Unknown internal error";
            response_body = err.dump();
        }
        std::stringstream http_resp;
        http_resp << "HTTP/1.1 200 OK\r\n"
                  << "Content-Type: application/json\r\n"
                  << "Content-Length: " << response_body.length() << "\r\n"
                  << "Connection: close\r\n\r\n"
                  << response_body;

        send_all(client, http_resp.str());
        shutdown(client, SD_SEND);
        char discard[128];
        while (recv(client, discard, sizeof(discard), 0) > 0);
        closesocket(client);
    }

    closesocket(server_sock);
}

std::string ConsensusNode::handle_request(const std::string& method,
                                           const std::string& path,
                                           const std::string& body) {
    json request;
    if (!body.empty()) {
        try { request = json::parse(body); } catch (...) {}
    }

    json response;

    // Node info
    if (path == "/api/node/info") {
        response["node_id"] = node_id;
        response["role"] = get_role_str();
        response["term"] = current_term.load();
        response["blocks"] = blockchain.chain_length();
        response["listener"] = listen_addr;
        response["leader_id"] = leader_id;
        response["election_in_progress"] = election_in_progress.load();
        response["has_active_leader"] = has_active_leader.load();
    }

    // Leader/failover status — for dashboards to render "voting in progress"
    else if (path == "/api/leader/status") {
        std::string ref = leader_id.empty() ? last_known_leader : leader_id;
        std::string successor = get_successor_id(ref);
        response["node_id"] = node_id;
        response["role"] = get_role_str();
        response["leader_id"] = leader_id;
        response["term"] = current_term.load();
        response["election_in_progress"] = election_in_progress.load();
        response["election_progress"] = election_progress.load();
        response["has_active_leader"] = has_active_leader.load();
        response["successor"] = successor;          // who takes over next
        response["is_successor"] = (successor == node_id);
        response["last_known_leader"] = last_known_leader;
        response["ordered_nodes"] = get_ordered_node_ids();
    }

    // Get full blockchain
    else if (path == "/api/blockchain/sync") {
        response["blocks"] = json::array();
        for (const auto& b : blockchain.get_all_blocks()) {
            response["blocks"].push_back(b.to_json());
        }
        response["validators"] = json::array();
        for (const auto& v : blockchain.get_validators()) {
            response["validators"].push_back({
                {"node_id", v.node_id},
                {"pub_key_hex", v.pub_key_hex},
                {"endpoint", v.endpoint}
            });
        }
        response["status"] = "OK";
        response["count"] = blockchain.chain_length();
    }

    // Raft: RequestVote (also handles pre-vote)
    else if (path == "/api/raft/request_vote") {
        bool is_pre_vote = request.value("pre_vote", false);
        RequestVoteArgs args;
        args.term = request.value("term", 0LL);
        args.candidate_id = request.value("candidate_id", "");
        args.last_log_index = request.value("last_log_index", 0LL);
        args.last_log_hash = request.value("last_log_hash", "");

        if (is_pre_vote) {
            // Pre-vote: check without persisting state
            std::lock_guard<std::mutex> lock(mutex);
            bool grant = false;
            if (args.term >= current_term.load()) {
                int64_t my_last = blockchain.chain_length();
                if (args.last_log_index >= my_last - 1) {
                    grant = true;
                }
            }
            response["term"] = current_term.load();
            response["vote_granted"] = grant;
            std::cout << "[NODE " << node_id << "] Pre-vote "
                      << (grant ? "GRANTED" : "DENIED") << " for "
                      << args.candidate_id.substr(0, 8) << " (term "
                      << args.term << ")" << std::endl;
        } else {
            auto result = handle_request_vote(args);
            response["term"] = result.term;
            response["vote_granted"] = result.vote_granted;
        }
    }

    // Raft: AppendEntries
    else if (path == "/api/raft/append_entries") {
        AppendEntriesArgs args;
        args.term = request.value("term", 0LL);
        args.leader_id = request.value("leader_id", "");
        args.prev_log_index = request.value("prev_log_index", 0LL);
        args.prev_log_hash = request.value("prev_log_hash", "");
        args.leader_commit = request.value("leader_commit", 0LL);
        if (request.contains("entries")) {
            for (const auto& ej : request["entries"]) {
                args.entries.push_back(Block::from_json(ej));
            }
        }
        auto result = handle_append_entries(args);
        response["term"] = result.term;
        response["success"] = result.success;
    }

    // Propose block (from GUI/admin)
    else if (path == "/api/blockchain/propose") {
        std::string file_hash = request.value("file_hash", "");
        std::string encrypted_label = request.value("encrypted_label", "");
        std::string student_name = request.value("student_name", "");
        std::string student_id = request.value("student_id", "");
        std::string encrypted_details = request.value("encrypted_details", "");

        bool ok = propose_block(file_hash, encrypted_label,
                                student_name, student_id, encrypted_details);
        response["status"] = ok ? "ACCEPTED" : "REJECTED";
    }

    // Peer handshake
    else if (path == "/api/peer/hello") {
        std::string peer_endpoint = request.value("endpoint", "");
        std::string peer_pub_key = request.value("pub_key", "");
        if (!peer_endpoint.empty()) {
            add_peer(peer_endpoint, peer_pub_key);
            // Register connecting peer as a validator
            if (!peer_pub_key.empty()) {
                std::string peer_id = ECDSAUtils::pub_key_to_short_id(peer_pub_key);
                if (!blockchain.has_validator(peer_id)) {
                    add_validator(peer_id, peer_pub_key, peer_endpoint);
                    std::cout << "[NODE " << node_id << "] Peer " << peer_id
                              << " registered as validator via handshake" << std::endl;
                }
            }
        }
        response["status"] = "OK";
        response["node_id"] = node_id;
        response["pub_key"] = blockchain.get_validators().empty() ? "" :
                              blockchain.get_validators()[0].pub_key_hex;
    }

    // Verify diploma across this node (for multi-node consensus)
    else if (path == "/api/blockchain/verify") {
        try {
            std::string file_hash = request.value("file_hash", "");
            std::string label = request.value("label", "");

            if (label.empty()) {
                response["status"] = "ERROR";
                response["message"] = "Missing 'label' field";
            } else {
                const Block* b = blockchain.find_by_label(label);
                if (b) {
                    response["status"] = "VERIFIED";
                    response["name"] = b->student_name;
                    response["id"] = b->student_id;
                    response["timestamp"] = b->timestamp;
                    response["file_hash"] = b->file_hash;
                    response["block_hash"] = b->block_hash;
                    response["details_hash"] = b->details_hash;
                    response["validator_sigs"] = b->validator_sigs.size();

                    // If file_hash provided, also verify file integrity
                    if (!file_hash.empty()) {
                        bool file_match = (b->file_hash == file_hash);
                        response["file_match"] = file_match;
                        if (!file_match) {
                            response["status"] = "FILE_MISMATCH";
                        }
                    }

                    // Check off-chain vault integrity
                    if (!b->details_hash.empty()) {
                        bool vault_ok = OffChainVault::verify_details(b->file_hash, b->details_hash);
                        response["vault_integrity"] = vault_ok;
                    }
                } else {
                    response["status"] = "NOT_FOUND";
                }
            }
        } catch (const std::exception& e) {
            response["status"] = "ERROR";
            response["message"] = e.what();
        }
    }

    // Multi-sig: sign a proposal from another validator
    else if (path == "/api/blockchain/sign_proposal") {
        try {
            Block proposal = Block::from_json(request);
            std::string proposer_id = request.value("proposer_id", "");
            std::string expected_hash = blockchain.calculate_block_hash(proposal);

            if (proposal.block_hash != expected_hash) {
                response["status"] = "REJECTED";
                response["message"] = "Block hash mismatch";
            } else {
                std::string sig = ECDSAUtils::sign(
                    blockchain.get_node_priv_key(),
                    proposal.block_hash
                );
                response["status"] = "signed";
                response["signature"] = sig;
                response["validator_id"] = node_id;
                std::cout << "[NODE " << node_id << "] Signed proposal from "
                          << proposer_id.substr(0, 12) << "... for block "
                          << proposal.index << std::endl;
            }
        } catch (const std::exception& e) {
            response["status"] = "ERROR";
            response["message"] = e.what();
        }
    }

    else {
        response["status"] = "ERROR";
        response["message"] = "Unknown path: " + path;
    }

    return response.dump();
}

// ── Raft Consensus ─────────────────────────────────────────────────────────

// Deterministic membership ordering: this node + all known peers, sorted by
// node_id. Every node computes the SAME order, so they agree on who the
// successor is without any coordination.
std::vector<std::string> ConsensusNode::get_ordered_node_ids() const {
    std::vector<std::string> ids;
    ids.push_back(node_id);
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        for (const auto& p : peers) {
            if (!p.node_id.empty()) ids.push_back(p.node_id);
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// Successor = the next node_id after `dead_leader` in the sorted ring
// (wraps around). This is the "+1" takeover: if node 3 dies, node 4 is next.
std::string ConsensusNode::get_successor_id(const std::string& dead_leader) const {
    std::vector<std::string> ids = get_ordered_node_ids();
    if (ids.empty()) return "";
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == dead_leader) {
            return ids[(i + 1) % ids.size()];  // wrap around the ring
        }
    }
    // Dead leader unknown (never seen) — first node in ring takes over.
    return ids.front();
}

bool ConsensusNode::am_i_successor() const {
    if (last_known_leader.empty()) return false;
    return get_successor_id(last_known_leader) == node_id;
}

// Endpoint lookup by node_id.
std::string ConsensusNode::endpoint_for_node(const std::string& target_id) const {
    std::lock_guard<std::mutex> lock(peers_mutex);
    for (const auto& p : peers) {
        if (p.node_id == target_id) return p.endpoint;
    }
    return "";
}

// The heart of instant failover: the successor gets the shortest timeout so it
// detects the dead leader first and wins in ~150ms of voting. We rank against
// the *current* leader while it's alive too, so the successor is always primed
// to take over. Non-successors fall back in ring order (base + step*rank).
int64_t ConsensusNode::compute_election_timeout() {
    std::vector<std::string> ids = get_ordered_node_ids();

    // Rank against the dead leader if known, otherwise the live one — either
    // way the designated successor keeps the shortest timer.
    std::string ref_leader = !last_known_leader.empty() ? last_known_leader : leader_id;

    if (!ref_leader.empty() && ids.size() > 1) {
        int leader_idx = -1;
        for (size_t i = 0; i < ids.size(); i++) {
            if (ids[i] == ref_leader) { leader_idx = (int)i; break; }
        }
        if (leader_idx >= 0) {
            int my_idx = -1;
            for (size_t i = 0; i < ids.size(); i++) {
                if (ids[i] == node_id) { my_idx = (int)i; break; }
            }
            if (my_idx >= 0) {
                // rank 0 = immediate successor (fastest), 1 = next, ...
                int rank = (my_idx - leader_idx - 1 + (int)ids.size()) % (int)ids.size();
                std::uniform_int_distribution<int> jitter(0, 100);
                return SUCCESSOR_BASE_MS + (int64_t)rank * SUCCESSOR_STEP_MS + jitter(rng);
            }
        }
    }

    // No leader reference (fresh cluster startup): short random jitter so the
    // cluster elects a first leader quickly without a vote-split storm.
    std::uniform_int_distribution<int> dist(FRESH_START_MIN_MS, FRESH_START_MAX_MS);
    return dist(rng);
}

void ConsensusNode::run_consensus_loop() {
    // Stabilize: wait for peer connections to establish before first election
    std::this_thread::sleep_for(std::chrono::milliseconds(RAFT_STARTUP_STABILIZE_MS));
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    // Cache the first election timeout (random, since no leader has died yet).
    cached_election_timeout = compute_election_timeout();

    while (running.load()) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t elapsed = now - last_heartbeat_time.load();

        switch (role.load()) {
            case NodeRole::FOLLOWER: {
                if (elapsed > cached_election_timeout) {
                    // Leader appears dead — remember it so we can compute the
                    // successor, flag the election, then run for office.
                    if (has_active_leader.load() && !leader_id.empty()) {
                        last_known_leader = leader_id;
                        leader_lost_time.store(now);
                        election_in_progress.store(true);
                        election_progress.store(0);
                        has_active_leader.store(false);
                        if (am_i_successor()) {
                            std::cout << "[NODE " << node_id << "] Leader "
                                      << last_known_leader.substr(0, 8)
                                      << " lost — I am the SUCCESSOR, taking over instantly"
                                      << std::endl;
                        }
                    }
                    become_candidate();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }
            case NodeRole::CANDIDATE: {
                int64_t elapsed_since_election = now - last_heartbeat_time.load();
                election_progress.store(50);
                if (elapsed_since_election > cached_election_timeout) {
                    become_candidate();
                }
                request_votes();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }
            case NodeRole::LEADER: {
                send_heartbeats_async();
                process_heartbeat_responses();
                if (should_check_peer_health()) {
                    check_peer_health();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(RAFT_HEARTBEAT_MS));
                break;
            }
        }
    }
}

void ConsensusNode::become_follower(int64_t term) {
    role.store(NodeRole::FOLLOWER);
    current_term.store(term);
    voted_for = "";
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    // Re-cache timeout: prioritizes us if we're the successor of a dead leader.
    cached_election_timeout = compute_election_timeout();
}

void ConsensusNode::become_candidate() {
    role.store(NodeRole::CANDIDATE);
    int64_t new_term = current_term.fetch_add(1) + 1;
    current_term.store(new_term);
    voted_for = node_id;
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    // Keep the priority timeout so the successor keeps its head start on retries.
    cached_election_timeout = compute_election_timeout();
    std::cout << "[NODE " << node_id << "] Starting election for term " << new_term << std::endl;
}

void ConsensusNode::become_leader() {
    role.store(NodeRole::LEADER);
    leader_id = node_id;
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    // Election settled — clear failover state.
    election_in_progress.store(false);
    election_progress.store(100);
    has_active_leader.store(true);
    last_known_leader = "";
    int64_t takeover_ms = leader_lost_time.load() > 0
        ? (last_heartbeat_time.load() - leader_lost_time.load()) : 0;
    std::cout << "[NODE " << node_id << "] Became LEADER for term "
              << current_term.load();
    if (takeover_ms > 0) {
        std::cout << " (takeover in " << takeover_ms << "ms)";
    }
    std::cout << std::endl;
    leader_lost_time.store(0);
    // Immediately assert leadership so followers stop their election timers.
    send_heartbeats_async();
}

ConsensusNode::RequestVoteResult
ConsensusNode::handle_request_vote(const RequestVoteArgs& args) {
    RequestVoteResult result;
    result.term = current_term.load();
    result.vote_granted = false;

    std::lock_guard<std::mutex> lock(mutex);

    if (args.term < current_term.load()) return result;

    if (args.term > current_term.load()) {
        become_follower(args.term);
    }

    // Pre-vote is also handled via this RPC
    // In the actual handler, we check for pre_vote in handle_request

    if (voted_for.empty() || voted_for == args.candidate_id) {
        int64_t my_last = blockchain.chain_length();
        if (args.last_log_index >= my_last - 1) {
            voted_for = args.candidate_id;
            result.vote_granted = true;
            last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }
    }

    return result;
}

ConsensusNode::AppendEntriesResult
ConsensusNode::handle_append_entries(const AppendEntriesArgs& args) {
    AppendEntriesResult result;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    result.term = current_term.load();
    result.success = false;

    std::lock_guard<std::mutex> lock(mutex);

    if (args.term < current_term.load()) return result;

    // Valid leader heartbeat — clear any in-progress failover state.
    has_active_leader.store(true);
    election_in_progress.store(false);
    election_progress.store(100);
    last_known_leader = "";
    leader_lost_time.store(0);

    become_follower(args.term);
    last_heartbeat_time.store(now);
    leader_id = args.leader_id;

    // Log heartbeat latency (empty entry = heartbeat)
    if (args.entries.empty()) {
        result.success = true;
        return result;
    }

    // Append entries
    int appended = 0;
    for (const auto& entry : args.entries) {
        if (!blockchain.has_block(entry.file_hash)) {
            if (blockchain.append_signed_block(entry)) {
                result.success = true;
                appended++;
                if (block_committed_cb) {
                    block_committed_cb(entry);
                }
                std::cout << "[NODE " << node_id << "] Replicated block "
                          << entry.index << " from leader " << args.leader_id.substr(0, 8)
                          << " (term " << args.term << ")" << std::endl;
            } else {
                std::cerr << "[NODE " << node_id << "] Failed to append block "
                          << entry.index << " from leader" << std::endl;
            }
        } else {
            result.success = true;
        }
    }

    if (appended > 0) {
        std::cout << "[NODE " << node_id << "] Appended " << appended
                  << " block(s) from leader, chain now "
                  << blockchain.chain_length() << " blocks" << std::endl;
    }

    return result;
}

void ConsensusNode::request_votes() {
    int64_t term = current_term.load();
    std::vector<Peer> current_peers = get_peers();

    int votes_needed = (current_peers.size() + 1) / 2 + 1;
    if (votes_needed < 2) votes_needed = 2;

    // Pre-vote check: skip election if we can't get enough pre-votes
    if (!request_pre_vote()) {
        // Pre-vote failed — reset election timer and stay candidate
        last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        return;
    }

    json req_body;
    req_body["term"] = term;
    req_body["candidate_id"] = node_id;
    req_body["last_log_index"] = blockchain.chain_length();
    req_body["last_log_hash"] = blockchain.get_chain_hash();

    std::string body = req_body.dump();

    // Exponential backoff: 2^attempt * 100ms, max 3 retries
    for (int attempt = 0; attempt < 3; attempt++) {
        std::vector<std::string> responses(current_peers.size());
        {
            std::vector<std::thread> threads;
            for (size_t i = 0; i < current_peers.size(); i++) {
                threads.emplace_back([this, &current_peers, &responses, i, &body]() {
                    // Check peer backoff
                    std::lock_guard<std::mutex> lock(peers_mutex);
                    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    if (now < peers[i].next_contact_time) return;
                    responses[i] = send_to_peer(current_peers[i].endpoint,
                                                "/api/raft/request_vote", body);
                });
            }
            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }
        }

        int votes_granted = 1;
        for (const auto& resp : responses) {
            if (resp.empty()) {
                mark_peer_failure(current_peers[&resp - &responses[0]].endpoint);
                continue;
            }
            try {
                json j = json::parse(resp);
                if (j.value("vote_granted", false)) {
                    mark_peer_success(current_peers[&resp - &responses[0]].endpoint);
                    votes_granted++;
                }
                if (j.value("term", 0LL) > term) {
                    become_follower(j["term"]);
                    return;
                }
            } catch (...) {
                mark_peer_failure(current_peers[&resp - &responses[0]].endpoint);
            }
        }

        if (votes_granted >= votes_needed) {
            std::cout << "[NODE " << node_id << "] Won election with "
                      << votes_granted << "/" << votes_needed << " votes (attempt "
                      << attempt + 1 << ")" << std::endl;
            become_leader();
            return;
        }

        if (attempt < 2) {
            int64_t delay_ms = (1LL << attempt) * 100; // 100, 200, 400ms
            std::cout << "[NODE " << node_id << "] Election attempt " << (attempt + 1)
                      << " failed (" << votes_granted << "/" << votes_needed
                      << ") — retrying in " << delay_ms << "ms" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    // All retries exhausted — stay as candidate, reset timer
    std::cout << "[NODE " << node_id << "] All election retries exhausted" << std::endl;
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void ConsensusNode::send_heartbeats() {
    send_heartbeats_async();
}

void ConsensusNode::send_heartbeats_async() {
    int64_t term = current_term.load();
    std::vector<Peer> current_peers = get_peers();
    if (current_peers.empty()) return;

    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    last_heartbeat_sent_time.store(now);

    json req_body;
    req_body["term"] = term;
    req_body["leader_id"] = node_id;
    req_body["prev_log_index"] = blockchain.chain_length();
    req_body["prev_log_hash"] = blockchain.get_chain_hash();
    req_body["leader_commit"] = blockchain.chain_length();
    req_body["entries"] = json::array();

    std::string body = req_body.dump();

    for (const auto& peer : current_peers) {
        // Skip unhealthy peers or peers in backoff
        if (!is_peer_healthy(peer.endpoint)) continue;
        if (now < peer.next_contact_time) continue;

        // Spawn async heartbeat — non-blocking for consensus loop
        std::thread([this, peer, term, body]() {
            std::string resp = send_to_peer(peer.endpoint,
                                             "/api/raft/append_entries",
                                             body);
            // Queue the response for later processing
            {
                std::lock_guard<std::mutex> lock(response_queue_mutex);
                heartbeat_responses.push({peer.endpoint, resp});
            }
        }).detach();
    }
}

void ConsensusNode::process_heartbeat_responses() {
    std::queue<std::pair<std::string, std::string>> q;
    {
        std::lock_guard<std::mutex> lock(response_queue_mutex);
        q.swap(heartbeat_responses);
    }

    int64_t term = current_term.load();
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    while (!q.empty()) {
        auto [endpoint, resp] = q.front();
        q.pop();

        // Update peer health
        if (resp.empty()) {
            mark_peer_failure(endpoint);
            std::lock_guard<std::mutex> plock(peer_health_mutex);
            peer_health_map[endpoint].consecutive_timeouts++;
            if (peer_health_map[endpoint].consecutive_timeouts >= 15) {
                peer_health_map[endpoint].status = PeerStatus::UNHEALTHY;
                std::cout << "[NODE " << node_id << "] Peer " << endpoint
                          << " marked UNHEALTHY (" << peer_health_map[endpoint].consecutive_timeouts
                          << " consecutive timeouts)" << std::endl;
            }
            continue;
        }

        try {
            json j = json::parse(resp);
            mark_peer_success(endpoint);

            int64_t rtt = now - last_heartbeat_sent_time.load();
            {
                std::lock_guard<std::mutex> plock(peer_health_mutex);
                auto& ph = peer_health_map[endpoint];
                ph.last_response_time = now;
                ph.last_success_time = now;
                ph.consecutive_timeouts = 0;
                ph.status = PeerStatus::HEALTHY;
                // Exponential moving average of RTT
                ph.avg_response_ms = ph.avg_response_ms * 0.9 + rtt * 0.1;
            }

            if (j.value("term", 0LL) > term) {
                std::cout << "[NODE " << node_id << "] Heartbeat response from "
                          << endpoint << " has higher term " << j["term"]
                          << " > " << term << " — demoting" << std::endl;
                become_follower(j["term"]);
                return;
            }
        } catch (...) {
            mark_peer_failure(endpoint);
        }
    }
}

bool ConsensusNode::should_check_peer_health() const {
    // Check every 10 heartbeat cycles
    static int counter = 0;
    return (++counter % 10 == 0);
}

bool ConsensusNode::is_peer_healthy(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lock(peer_health_mutex);
    auto it = peer_health_map.find(endpoint);
    if (it == peer_health_map.end()) return true; // unknown peers are healthy
    return it->second.status == PeerStatus::HEALTHY;
}

void ConsensusNode::check_peer_health() {
    std::vector<std::string> unhealthy_endpoints;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(peer_health_mutex);
        for (auto& [ep, ph] : peer_health_map) {
            if (ph.status == PeerStatus::UNHEALTHY) {
                // Ping the peer
                std::string resp = send_to_peer(ep, "/api/node/info", "");
                if (!resp.empty()) {
                    try {
                        json j = json::parse(resp);
                        ph.status = PeerStatus::HEALTHY;
                        ph.consecutive_timeouts = 0;
                        ph.last_success_time = now;
                        std::cout << "[NODE " << node_id << "] Peer " << ep
                                  << " recovered — marked HEALTHY" << std::endl;
                    } catch (...) {
                        ph.consecutive_timeouts++;
                    }
                } else {
                    ph.consecutive_timeouts++;
                }
            }
            // Mark as DEAD after 60 consecutive failures
            if (ph.consecutive_timeouts >= 60) {
                ph.status = PeerStatus::DEAD;
                std::cout << "[NODE " << node_id << "] Peer " << ep
                          << " marked DEAD after " << ph.consecutive_timeouts
                          << " failures" << std::endl;
            }
        }
    }
}

bool ConsensusNode::request_pre_vote() {
    int64_t term = current_term.load();
    std::vector<Peer> current_peers = get_peers();

    pre_vote_responses.clear();

    json req_body;
    req_body["pre_vote"] = true;
    req_body["term"] = term;
    req_body["candidate_id"] = node_id;
    req_body["last_log_index"] = blockchain.chain_length();
    req_body["last_log_hash"] = blockchain.get_chain_hash();

    std::string body = req_body.dump();

    // Parallel pre-vote (same pattern as actual vote)
    std::vector<std::string> responses(current_peers.size());
    {
        std::vector<std::thread> threads;
        for (size_t i = 0; i < current_peers.size(); i++) {
            threads.emplace_back([this, &current_peers, &responses, i, &body]() {
                responses[i] = send_to_peer(current_peers[i].endpoint,
                                            "/api/raft/request_vote", body);
            });
        }
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }

    int grants = 1; // self
    for (const auto& resp : responses) {
        if (!resp.empty()) {
            try {
                json j = json::parse(resp);
                if (j.value("vote_granted", false)) {
                    grants++;
                }
            } catch (...) {}
        }
    }

    int needed = (current_peers.size() + 1) / 2 + 1;
    if (needed < 2) needed = 2;
    bool won = (grants >= needed);

    if (!won) {
        std::cout << "[NODE " << node_id << "] Pre-vote lost ("
                  << grants << "/" << needed << ") — skipping election" << std::endl;
    }
    return won;
}

void ConsensusNode::replicate_log() {
    // Called after proposing a block - send it to all followers
    if (role.load() != NodeRole::LEADER) return;

    Block last_block = blockchain.get_last_block();
    int64_t term = current_term.load();
    std::vector<Peer> current_peers = get_peers();

    json entries = json::array();
    entries.push_back(last_block.to_json());

    json req_body;
    req_body["term"] = term;
    req_body["leader_id"] = node_id;
    req_body["prev_log_index"] = blockchain.chain_length() - 1;
    req_body["prev_log_hash"] = last_block.previous_hash;
    req_body["leader_commit"] = blockchain.chain_length();
    req_body["entries"] = entries;

    for (const auto& peer : current_peers) {
        std::string resp = send_to_peer(peer.endpoint,
                                        "/api/raft/append_entries",
                                        req_body.dump());
        if (resp.empty()) {
            std::cerr << "[NODE " << node_id << "] replicate_log to "
                      << peer.endpoint << " FAILED (no response)" << std::endl;
        } else {
            try {
                json j = json::parse(resp);
                if (!j.value("success", false)) {
                    std::cerr << "[NODE " << node_id << "] replicate_log to "
                              << peer.endpoint << " rejected (success=false)"
                              << std::endl;
                }
            } catch (...) {
                std::cerr << "[NODE " << node_id << "] replicate_log to "
                          << peer.endpoint << " bad JSON response" << std::endl;
            }
        }
    }
}

// ── Blockchain Operations ─────────────────────────────────────────────────

bool ConsensusNode::propose_block(const std::string& file_hash,
                                   const std::string& encrypted_label,
                                   const std::string& student_name,
                                   const std::string& student_id,
                                   const std::string& encrypted_details) {

    // Upload resilience: if a failover is in progress there may be no leader
    // for a brief moment. Instead of failing the upload, wait through the
    // fast-failover window (~up to 3s) for a leader to emerge or for us to be
    // promoted. This is why priority failover matters — the wait is short.
    if (role.load() != NodeRole::LEADER) {
        for (int i = 0; i < 30; i++) {  // 30 * 100ms = 3s max
            if (role.load() == NodeRole::LEADER) break;
            if (!leader_id.empty() && has_active_leader.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (role.load() == NodeRole::LEADER) {
        std::lock_guard<std::mutex> lock(mutex);

        Block b = blockchain.create_unsigned_block(
            file_hash, encrypted_label, student_name, student_id,
            encrypted_details, current_term.load());

        blockchain.sign_block(b);

        if (blockchain.append_signed_block(b)) {
            blockchain.save_to_file(blockchain_file);
            replicate_log();
            if (block_committed_cb) block_committed_cb(b);
            std::cout << "[NODE " << node_id << "] Block committed (index "
                      << b.index << ")" << std::endl;
            return true;
        }
        return false;
    }

    // Forward to leader. leader_id is a node_id, so match peers by node_id
    // (endpoint match was a latent bug — leader_id is never an endpoint).
    if (!leader_id.empty()) {
        json req;
        req["file_hash"] = file_hash;
        req["encrypted_label"] = encrypted_label;
        req["student_name"] = student_name;
        req["student_id"] = student_id;
        req["encrypted_details"] = encrypted_details;
        std::string body = req.dump();

        std::string leader_ep = endpoint_for_node(leader_id);
        if (!leader_ep.empty()) {
            std::string resp = send_to_peer(leader_ep, "/api/blockchain/propose", body);
            if (!resp.empty()) {
                try {
                    json j = json::parse(resp);
                    if (j.value("status", "") == "ACCEPTED") return true;
                } catch (...) {}
            }
        }

        // Fallback: leader endpoint unknown or failed — try every peer until
        // one (the real leader) accepts.
        std::vector<Peer> current_peers = get_peers();
        for (const auto& peer : current_peers) {
            if (peer.endpoint == leader_ep) continue;  // already tried
            std::string resp = send_to_peer(peer.endpoint, "/api/blockchain/propose", body);
            if (!resp.empty()) {
                try {
                    json j = json::parse(resp);
                    if (j.value("status", "") == "ACCEPTED") return true;
                } catch (...) {}
            }
        }
    }

    std::cerr << "[NODE] Cannot propose: no leader known" << std::endl;
    return false;
}

void ConsensusNode::mark_peer_success(const std::string& endpoint) {
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        for (auto& p : peers) {
            if (p.endpoint == endpoint) {
                p.consecutive_failures = 0;
                p.next_contact_time = 0;
                p.is_active = true;
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(peer_health_mutex);
        auto& ph = peer_health_map[endpoint];
        ph.consecutive_timeouts = 0;
        ph.status = PeerStatus::HEALTHY;
    }
}

void ConsensusNode::mark_peer_failure(const std::string& endpoint) {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        for (auto& p : peers) {
            if (p.endpoint == endpoint) {
                p.consecutive_failures++;
                // Exponential backoff: 2^failures * 100ms, max 5s
                int64_t backoff_ms = std::min(
                    (1LL << std::min(p.consecutive_failures, 6)) * 100LL,
                    5000LL);
                p.next_contact_time = now + backoff_ms;
                p.is_active = false;
                break;
            }
        }
    }
}

bool ConsensusNode::is_peer_reachable(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lock(peers_mutex);
    for (const auto& p : peers) {
        if (p.endpoint == endpoint) {
            int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return now >= p.next_contact_time;
        }
    }
    return true;
}

void ConsensusNode::reset_election_timer() {
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    // Re-cache election timeout for this term
    std::uniform_int_distribution<int> dist(RAFT_ELECTION_MIN_MS, RAFT_ELECTION_MAX_MS);
    cached_election_timeout = dist(rng);
}

void ConsensusNode::add_validator(const std::string& v_node_id,
                                   const std::string& v_pub_key_hex,
                                   const std::string& v_endpoint) {
    Validator v;
    v.node_id = v_node_id;
    v.pub_key_hex = v_pub_key_hex;
    v.endpoint = v_endpoint;
    blockchain.add_validator(v);
    std::cout << "[NODE " << node_id << "] Validator added: "
              << v_node_id << " @ " << v_endpoint << std::endl;
}

// ── Networking ─────────────────────────────────────────────────────────────

std::string ConsensusNode::send_to_peer(const std::string& endpoint,
                                         const std::string& path,
                                         const std::string& body) {
    return http_post("http://" + endpoint + path, body);
}

void ConsensusNode::broadcast_to_peers(const std::string& path,
                                        const std::string& body) {
    std::vector<Peer> current_peers = get_peers();
    for (const auto& peer : current_peers) {
        send_to_peer(peer.endpoint, path, body);
    }
}

void ConsensusNode::broadcast_except(const std::string& exclude_endpoint,
                                      const std::string& path,
                                      const std::string& body) {
    std::vector<Peer> current_peers = get_peers();
    for (const auto& peer : current_peers) {
        if (peer.endpoint != exclude_endpoint) {
            send_to_peer(peer.endpoint, path, body);
        }
    }
}

void ConsensusNode::sync_from_peer(const Peer& peer) {
    std::string resp = send_to_peer(peer.endpoint, "/api/blockchain/sync", "");
    if (resp.empty()) return;

    try {
        json j = json::parse(resp);
        if (j.contains("blocks")) {
            for (const auto& bj : j["blocks"]) {
                Block b = Block::from_json(bj);
                if (!blockchain.has_block(b.file_hash)) {
                    blockchain.append_signed_block(b);
                }
            }
            std::cout << "[NODE " << node_id << "] Synced "
                      << j["blocks"].size() << " blocks from "
                      << peer.endpoint << std::endl;
        }
    } catch (...) {}
}

std::string ConsensusNode::serialize_blockchain() const {
    json j;
    j["blocks"] = json::array();
    for (const auto& b : blockchain.get_all_blocks()) {
        j["blocks"].push_back(b.to_json());
    }
    return j.dump();
}

std::string ConsensusNode::get_status_json() const {
    json j;
    j["node_id"] = node_id;
    j["role"] = get_role_str();
    j["term"] = current_term.load();
    j["blocks"] = blockchain.chain_length();
    j["listener"] = listen_addr;
    j["leader_id"] = leader_id;
    j["election_in_progress"] = election_in_progress.load();
    j["election_progress"] = election_progress.load();
    j["has_active_leader"] = has_active_leader.load();
    {
        std::string ref = leader_id.empty() ? last_known_leader : leader_id;
        std::string successor = get_successor_id(ref);
        j["successor"] = successor;
        j["is_successor"] = (successor == node_id);
    }

    j["peers"] = json::array();
    for (const auto& p : get_peers()) {
        j["peers"].push_back({
            {"node_id", p.node_id},
            {"endpoint", p.endpoint},
            {"is_active", p.is_active},
            {"is_leader", p.is_leader}
        });
    }

    j["validators"] = json::array();
    for (const auto& v : blockchain.get_validators()) {
        j["validators"].push_back({
            {"node_id", v.node_id},
            {"pub_key_hex", v.pub_key_hex.substr(0, 32) + "..."}
        });
    }

    return j.dump();
}
