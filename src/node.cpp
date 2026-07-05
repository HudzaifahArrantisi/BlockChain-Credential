#include "../include/node.h"
#include "../include/ecdsa_utils.h"
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
static const int RAFT_HEARTBEAT_MS = 500;
static const int RAFT_ELECTION_MIN_MS = 800;
static const int RAFT_ELECTION_MAX_MS = 1600;
static const int HTTP_BUFFER_SIZE = 65536;

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
    tv.tv_sec = 2;
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

    hostent* he = gethostbyname(host.c_str());
    if (!he) { closesocket(sock); return ""; }

    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    memcpy(&sin.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        closesocket(sock);
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

    // Use blocking accept with select timeout for clean shutdown
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

        std::string response_body = handle_request(method, path, body);
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

    // Raft: RequestVote
    else if (path == "/api/raft/request_vote") {
        RequestVoteArgs args;
        args.term = request.value("term", 0LL);
        args.candidate_id = request.value("candidate_id", "");
        args.last_log_index = request.value("last_log_index", 0LL);
        args.last_log_hash = request.value("last_log_hash", "");
        auto result = handle_request_vote(args);
        response["term"] = result.term;
        response["vote_granted"] = result.vote_granted;
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

    else {
        response["status"] = "ERROR";
        response["message"] = "Unknown path: " + path;
    }

    return response.dump();
}

// ── Raft Consensus ─────────────────────────────────────────────────────────

void ConsensusNode::run_consensus_loop() {
    std::uniform_int_distribution<int> election_jitter(RAFT_ELECTION_MIN_MS, RAFT_ELECTION_MAX_MS);

    while (running.load()) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t elapsed = now - last_heartbeat_time.load();

        switch (role.load()) {
            case NodeRole::FOLLOWER: {
                if (elapsed > election_jitter(rng)) {
                    become_candidate();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }
            case NodeRole::CANDIDATE: {
                int64_t elapsed_since_election = now - last_heartbeat_time.load();
                if (elapsed_since_election > election_jitter(rng)) {
                    become_candidate();
                }
                request_votes();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }
            case NodeRole::LEADER: {
                send_heartbeats();
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
}

void ConsensusNode::become_candidate() {
    role.store(NodeRole::CANDIDATE);
    int64_t new_term = current_term.fetch_add(1) + 1;
    current_term.store(new_term);
    voted_for = node_id;
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    std::cout << "[NODE " << node_id << "] Starting election for term " << new_term << std::endl;
}

void ConsensusNode::become_leader() {
    role.store(NodeRole::LEADER);
    leader_id = node_id;
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    std::cout << "[NODE " << node_id << "] Became LEADER for term "
              << current_term.load() << std::endl;
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

    if (voted_for.empty() || voted_for == args.candidate_id) {
        // Check log is at least as up-to-date
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
    result.term = current_term.load();
    result.success = false;

    std::lock_guard<std::mutex> lock(mutex);

    if (args.term < current_term.load()) return result;

    become_follower(args.term);
    last_heartbeat_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    leader_id = args.leader_id;

    // Accept empty heartbeat
    if (args.entries.empty()) {
        result.success = true;
        return result;
    }

    // Append entries
    for (const auto& entry : args.entries) {
        if (!blockchain.has_block(entry.file_hash)) {
            if (blockchain.append_signed_block(entry)) {
                result.success = true;
                if (block_committed_cb) {
                    block_committed_cb(entry);
                }
                std::cout << "[NODE " << node_id << "] Replicated block "
                          << entry.index << " from leader" << std::endl;
            }
        } else {
            result.success = true; // Already have it
        }
    }

    return result;
}

void ConsensusNode::request_votes() {
    int64_t term = current_term.load();
    std::vector<Peer> current_peers = get_peers();

    int votes_needed = (current_peers.size() + 1) / 2 + 1;
    int votes_granted = 1; // Vote for self

    json req_body;
    req_body["term"] = term;
    req_body["candidate_id"] = node_id;
    req_body["last_log_index"] = blockchain.chain_length();
    req_body["last_log_hash"] = blockchain.get_chain_hash();

    for (const auto& peer : current_peers) {
        std::string resp = send_to_peer(peer.endpoint,
                                         "/api/raft/request_vote",
                                         req_body.dump());
        if (!resp.empty()) {
            try {
                json j = json::parse(resp);
                if (j.value("vote_granted", false)) {
                    votes_granted++;
                }
                if (j.value("term", 0LL) > term) {
                    become_follower(j["term"]);
                    return;
                }
            } catch (...) {}
        }
    }

    if (votes_granted >= votes_needed) {
        become_leader();
    }
    // Not enough votes: stay as CANDIDATE, will retry with new term
    // after election timeout from run_consensus_loop
}

void ConsensusNode::send_heartbeats() {
    int64_t term = current_term.load();
    std::vector<Peer> current_peers = get_peers();

    json req_body;
    req_body["term"] = term;
    req_body["leader_id"] = node_id;
    req_body["prev_log_index"] = blockchain.chain_length();
    req_body["prev_log_hash"] = blockchain.get_chain_hash();
    req_body["leader_commit"] = blockchain.chain_length();
    req_body["entries"] = json::array();

    for (auto& peer : current_peers) {
        std::string resp = send_to_peer(peer.endpoint,
                                         "/api/raft/append_entries",
                                         req_body.dump());
        if (!resp.empty()) {
            try {
                json j = json::parse(resp);
                peer.is_active = true;
                if (j.value("term", 0LL) > term) {
                    become_follower(j["term"]);
                    // Update peers list with activity status
                    {
                        std::lock_guard<std::mutex> lock(peers_mutex);
                        for (auto& p : peers) {
                            if (p.endpoint == peer.endpoint) {
                                p.is_active = peer.is_active;
                                break;
                            }
                        }
                    }
                    return;
                }
            } catch (...) {
                peer.is_active = false;
            }
        }
        // Sync activity back to main peers list
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            for (auto& p : peers) {
                if (p.endpoint == peer.endpoint) {
                    p.is_active = peer.is_active;
                    break;
                }
            }
        }
    }
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
        send_to_peer(peer.endpoint, "/api/raft/append_entries", req_body.dump());
    }
}

// ── Blockchain Operations ─────────────────────────────────────────────────

bool ConsensusNode::propose_block(const std::string& file_hash,
                                   const std::string& encrypted_label,
                                   const std::string& student_name,
                                   const std::string& student_id,
                                   const std::string& encrypted_details) {

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

    // Forward to leader
    if (!leader_id.empty()) {
        json req;
        req["file_hash"] = file_hash;
        req["encrypted_label"] = encrypted_label;
        req["student_name"] = student_name;
        req["student_id"] = student_id;
        req["encrypted_details"] = encrypted_details;

        std::vector<Peer> current_peers = get_peers();
        for (const auto& peer : current_peers) {
            if (peer.is_leader || peer.endpoint == leader_id) {
                std::string resp = send_to_peer(peer.endpoint,
                                                 "/api/blockchain/propose",
                                                 req.dump());
                if (!resp.empty()) {
                    try {
                        json j = json::parse(resp);
                        return j.value("status", "") == "ACCEPTED";
                    } catch (...) {}
                }
            }
        }
    }

    std::cerr << "[NODE] Cannot propose: no leader known" << std::endl;
    return false;
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
