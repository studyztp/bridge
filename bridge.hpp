#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <cassert>
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

#if DEBUG
#define DPRINTF(...) do { std::printf(__VA_ARGS__); } while (0)
#else
#define DPRINTF(...) do { } while (0)
#endif

#define MAX_BUFFER_SIZE 1024

#define WARNING_MSG(...) \
    do { std::fprintf(stderr, "WARNING: "); \
         std::fprintf(stderr, __VA_ARGS__); } while (0)

enum COMMAND {
    INVALID,
    INITIALIZE_SERVER,
    INITIALIZE_CLIENT,
    COMPUTE_REQUEST,
    COMPUTE_RESPONSE
};
// This is used to setup connections between the matching simulations.
// This is needed because it can avoid adding extra parameters to each
// simulation process.
#define BRIDGE_SETUP_SOCKET_PATH "/tmp/bridge_socket.sock"

struct Message {
    COMMAND command = INVALID;
    std::vector<uint8_t> data;
    size_t length() const { return data.size(); }
};

// General socket functions
int set_nonblocking(int fd);
int setup_server_socket(const std::string& socket_path);
pid_t get_fd_pid(int fd);

// Function to setup the bridge helper socket that communicates between 
// simulations during the initialization phase.
// This server is meant to be closed after the initialization is done.
int bridge_setup_helper_server_socket();
int bridge_close_helper_server_socket(int listen_fd);
int bridge_helper_server_loop(int listen_fd, 
    std::unordered_map<std::string, std::string> client_to_server_match);

// Client functions
int bridge_setup_client(
                std::string client_name, pid_t& server_pid, int& socket_fd);

// Server functions
int bridge_setup_server(std::string server_name, pid_t& client_pid, 
                                                            int& socket_fd);

// General communication functions
int bridge_send_and_wait_for_response(int socket_fd, 
            const Message &msg_to_send, Message& response_msg, int timeout_ms);
Message bridge_wait_for_message(int socket_fd, int timeout_ms);
int bridge_send_message(int socket_fd, const Message &msg);
int bridge_send_interrupt(pid_t target);

// Helper functions for converting between Message and raw byte buffers
bool convert_data_to_message(const uint8_t* data, size_t length, Message& msg);
void convert_message_to_data(const Message& msg, size_t& out_length, 
                                                            uint8_t*& data);
std::string convert_bytes_to_string(const uint8_t* bytes, size_t length);
