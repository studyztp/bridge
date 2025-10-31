#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <signal.h>
#include <stdint.h>
#include <unordered_set>
#include <memory>

enum COMMAND {
    INVALID,
    INITIALIZE,
    ASK_FOR_COMPUTE,
    COMPUTE_FINISH,
    COMPUTE_IN_PROGRESS,
    COMPUTE_DATA_READY,
    ASK_FOR_SCHEDULE_STOP,
    WAIT_FOR_ROBOT_SIGNAL
};
#define BRIDGE_SOCKET_PATH "/tmp/bridge_socket.sock"

// Message holds an owned payload buffer.
struct Message {
    COMMAND command = INVALID;
    std::vector<uint8_t> data;
    size_t length() const { return data.size(); }
};

struct ClientConnection {
    int fd = -1;
    int id = 0;
    pid_t pid = -1;
    std::string name;
    Message message;
    // incoming byte buffer for this connection (used to handle message framing)
    std::vector<uint8_t> recv_buffer;
};

// reactions (take shared_ptr<ClientConnection> so implementations can register/lookup easily)
void initialize_client_name(std::shared_ptr<ClientConnection> client);
void ask_for_compute(std::shared_ptr<ClientConnection> client);
void compute_finish(std::shared_ptr<ClientConnection> client);
void compute_in_progress(std::shared_ptr<ClientConnection> client);
void compute_data_ready();
void ask_for_schedule_stop();
void wait_for_robot_signal();

// Helper hash for std::pair of strings (used by simulation_connections)
struct PairStringHash {
    size_t operator()(
                const std::pair<std::string, std::string>& p) const noexcept {
        // combine two std::hash results
        size_t h1 = std::hash<std::string>{}(p.first);
        size_t h2 = std::hash<std::string>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1<<6) + (h1>>2));
    }
};

// Map client name -> shared_ptr<ClientConnection> for fast lookup by name
static std::unordered_map<std::string, std::shared_ptr<ClientConnection>> clients_set;
// Map fd -> shared_ptr<ClientConnection> authoritative storage
static std::unordered_map<int, std::shared_ptr<ClientConnection>> clients;

// Map of simulation connections: client_name -> targeted_client_name
// Declare as extern here; define the single instance in bridge.cpp so all
// translation units (server and bridge implementation) share the same map.
extern std::unordered_map<std::string, std::string> simulation_connections;

[[noreturn]] void fatal_error(const char* msg);

void set_nonblocking(int fd);

std::string convert_bytes_to_string(const uint8_t* bytes, size_t length);

// cleanup
void cleanup_bridge(int bridge_socket_fd);

void bridge_client_send_message(int client_fd, const uint8_t* message, size_t length);

void bridge_server_send_message(int client_fd, const uint8_t* message, size_t length);

// Retrieve the PID of the peer connected on a Unix-domain socket.
// Returns the pid on success, or -1 on failure.
pid_t get_peer_pid(int fd);

void interrupt_client(std::string name);

// Parse raw buffer into Message. Wire format: 1 byte command followed by payload.
bool convert_data_to_message(const uint8_t* data, size_t length, Message& msg);

// Serialize Message into raw buffer (allocated with new[]). Caller receives ownership
// of `data` and must delete[] it when done.
void convert_message_to_data(const Message& msg, size_t& out_length, uint8_t*& data);

// Attempt to extract a framed Message from the start of a byte buffer.
// The buffer will be modified (consumed) on success. See bridge.cpp for
// the framing format (4-byte network-order length + 1-byte command + payload).
bool try_extract_message(std::vector<uint8_t>& buf, Message& out_msg);

// Declarations for functions implemented in bridge.cpp
int setup_bridge_server_socket();
void accept_new_client(int listen_fd, std::vector<pollfd>& poll_fds, int& next_client_id);
void run_bridge_server_loop(int listen_fd);
void setup_bridge_server();
int setup_bridge_client(std::string client_name);
Message bridge_client_send_and_wait_response(int client_fd, const uint8_t* message, size_t length, int timeout_ms = -1);
