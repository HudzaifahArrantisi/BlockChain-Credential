#ifndef NODE_H
#define NODE_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <map>
#include <queue>
#include "blockchain.h"

struct Peer {
    std::string node_id;
    std::string pub_key_hex;
    std::string endpoint;
    bool is_active = false;
    bool is_leader = false;
    int64_t last_heartbeat = 0;

    // Health tracking for exponential backoff
    int consecutive_failures = 0;
    int64_t next_contact_time = 0;  // ms timestamp; skip peer until this time
};

// Peer health state for the health-check subsystem
enum class PeerStatus { HEALTHY, UNHEALTHY, DEAD };

struct PeerHealth {
    PeerStatus status = PeerStatus::HEALTHY;
    int64_t last_response_time = 0;
    int64_t last_success_time = 0;
    int consecutive_timeouts = 0;
    double avg_response_ms = 0.0;
};

class ConsensusNode {
public:
    ConsensusNode(const std::string& listen_addr,
                  const std::string& data_dir,
                  const std::string& priv_key,
                  const std::string& pub_key);

    ~ConsensusNode();

    // Lifecycle
    void start();
    void stop();
    bool is_running() const { return running.load(); }

    // Peer management
    void add_peer(const std::string& endpoint, const std::string& pub_key_hex = "",
                  const std::string& peer_node_id = "");
    void remove_peer(const std::string& endpoint);
    std::vector<Peer> get_peers() const;
    void set_known_peers(const std::vector<std::string>& endpoints);

    // Blockchain operations
    bool propose_block(const std::string& file_hash,
                       const std::string& encrypted_label,
                       const std::string& student_name,
                       const std::string& student_id,
                       const std::string& encrypted_details = "");
    void add_validator(const std::string& node_id,
                       const std::string& pub_key_hex,
                       const std::string& endpoint);

    StrictBlockchain& get_blockchain() { return blockchain; }
    const StrictBlockchain& get_blockchain() const { return blockchain; }

    // Status
    NodeRole get_role() const { return role.load(); }
    std::string get_role_str() const;
    int64_t get_current_term() const { return current_term.load(); }
    std::string get_node_id() const { return node_id; }
    std::string get_listen_addr() const { return listen_addr; }

    // Callbacks for integration with GUI/external
    using BlockCallback = std::function<void(const Block&)>;
    void on_block_committed(BlockCallback cb) { block_committed_cb = cb; }

    std::string get_status_json() const;

    // HTTP helpers (public for external use)
    static std::string http_post(const std::string& url, const std::string& body);
    static std::string http_get(const std::string& url, const std::string& body = "");

private:
    // Raft core
    std::atomic<NodeRole> role{NodeRole::FOLLOWER};
    std::atomic<int64_t> current_term{0};
    std::string voted_for;
    std::string leader_id;

    std::atomic<int64_t> last_heartbeat_time{0};
    std::atomic<int64_t> election_timeout{1500};

    // Cached election timeout — set once per role transition, not re-rolled
    int64_t cached_election_timeout = 0;
    std::atomic<bool> has_active_leader{false};

    // ── Instant priority-based failover ──────────────────────────────
    // When the leader dies, the deterministic "successor" (next node in
    // sorted node-id order after the dead leader) uses a near-zero election
    // timeout so it wins the election in ~150ms. Voting still happens in the
    // background (Raft safety preserved), but it is effectively instant.
    std::string last_known_leader;                  // id of leader before it died
    std::atomic<bool> election_in_progress{false};  // true while cluster has no confirmed leader
    std::atomic<int> election_progress{0};          // 0-100, for UI/status feedback
    std::atomic<int64_t> leader_lost_time{0};       // ms timestamp leader was detected dead

    // Async heartbeat tracking
    std::queue<std::pair<std::string, std::string>> heartbeat_responses;
    std::mutex response_queue_mutex;
    std::atomic<int64_t> last_heartbeat_sent_time{0};

    // Peer health tracking
    std::map<std::string, PeerHealth> peer_health_map;
    mutable std::mutex peer_health_mutex;

    // Pre-vote mechanism
    std::map<std::string, int64_t> pre_vote_responses;

    // State
    std::string node_id;
    std::string listen_addr;
    std::string data_dir;
    std::string blockchain_file;
    StrictBlockchain blockchain;

    std::atomic<bool> running{false};
    mutable std::mutex mutex;
    mutable std::mutex peers_mutex;

    std::vector<Peer> peers;
    std::map<std::string, std::string> known_peers;

    std::thread server_thread;
    std::thread consensus_thread;

    BlockCallback block_committed_cb;

    // Server
    void run_server();
    std::string handle_request(const std::string& method, const std::string& path,
                                const std::string& body);

    // Consensus
    void run_consensus_loop();
    void become_follower(int64_t term);
    void become_candidate();
    void become_leader();
    void request_votes();
    void send_heartbeats();
    void send_heartbeats_async();
    void process_heartbeat_responses();
    void replicate_log();

    // Peer health helpers
    void mark_peer_success(const std::string& endpoint);
    void mark_peer_failure(const std::string& endpoint);
    void check_peer_health();
    bool should_check_peer_health() const;
    bool is_peer_healthy(const std::string& endpoint) const;
    bool is_peer_reachable(const std::string& endpoint) const;
    void reset_election_timer();

    // Pre-vote
    bool request_pre_vote();

    // ── Priority-based failover helpers ──────────────────────────────
    // Deterministic ordering of all cluster members (this node + peers) by
    // node_id. Used to pick the successor when the leader dies.
    std::vector<std::string> get_ordered_node_ids() const;
    // The node_id that should take over if `dead_leader` fails (next in ring).
    std::string get_successor_id(const std::string& dead_leader) const;
    // True if THIS node is the designated successor for the current dead leader.
    bool am_i_successor() const;
    // Election timeout for this node right now: tiny for the successor,
    // normal-random for everyone else. Keeps Raft safety while making the
    // successor win almost instantly.
    int64_t compute_election_timeout();
    // Resolve a node_id to its endpoint (empty if unknown).
    std::string endpoint_for_node(const std::string& target_id) const;

    // Raft RPCs
    struct RequestVoteArgs {
        int64_t term = 0;
        std::string candidate_id;
        int64_t last_log_index = 0;
        std::string last_log_hash;
    };

    struct RequestVoteResult {
        int64_t term = 0;
        bool vote_granted = false;
    };

    struct AppendEntriesArgs {
        int64_t term = 0;
        std::string leader_id;
        int64_t prev_log_index = 0;
        std::string prev_log_hash;
        std::vector<Block> entries;
        int64_t leader_commit = 0;
    };

    struct AppendEntriesResult {
        int64_t term = 0;
        bool success = false;
    };

    RequestVoteResult handle_request_vote(const RequestVoteArgs& args);
    AppendEntriesResult handle_append_entries(const AppendEntriesArgs& args);

    // Networking
    std::string send_to_peer(const std::string& endpoint, const std::string& path,
                              const std::string& body);
    void broadcast_to_peers(const std::string& path, const std::string& body);
    void broadcast_except(const std::string& exclude_endpoint,
                           const std::string& path, const std::string& body);

    // Sync
    void sync_from_peer(const Peer& peer);
    std::string serialize_blockchain() const;

    // Helpers
    static std::vector<std::string> split_string(const std::string& s, char delim);
    static std::string trim(const std::string& s);
};

#endif
