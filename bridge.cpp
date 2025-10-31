#include "bridge.hpp"
#include <arpa/inet.h>

// Single, shared definition of the simulation connections map declared
// extern in bridge.hpp. This ensures server.cpp (examples) and
// bridge.cpp use the same map instance.
std::unordered_map<std::string, std::string> simulation_connections;

[[noreturn]] void fatal_error(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        fatal_error("fcntl F_GETFL");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fatal_error("fcntl F_SETFL");
    }
}

std::string convert_bytes_to_string(const uint8_t* bytes, size_t length) {
    return std::string(reinterpret_cast<const char*>(bytes), length);
}

// cleanup
void cleanup_bridge(int bridge_socket_fd) {
    if (bridge_socket_fd >= 0) {
        close(bridge_socket_fd);
        unlink(BRIDGE_SOCKET_PATH);
    }
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->second && it->second->fd >= 0) close(it->second->fd);
    }
}

void bridge_client_send_message(int client_fd, const uint8_t* message, size_t length) {
    ssize_t bytes_sent = write(client_fd, message, length);
    if (bytes_sent < 0) {
        fatal_error("write");
    }
    DPRINTF("Sent %zd bytes to bridge server\n", bytes_sent);
}

void bridge_server_send_message(int client_fd, const uint8_t* message, size_t length) {
    ssize_t bytes_sent = write(client_fd, message, length);
    if (bytes_sent < 0) {
        fatal_error("write");
    }
    DPRINTF("Sent %zd bytes to client %d\n", bytes_sent, client_fd);
}

pid_t get_peer_pid(int fd) {
#ifdef SO_PEERCRED
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1) {
        perror("getsockopt SO_PEERCRED");
        return -1;
    }
    return cred.pid;
#else
    (void)fd;
    std::fprintf(stderr, "get_peer_pid: SO_PEERCRED not available on this platform\n");
    return -1;
#endif
}

void interrupt_client(std::string name) {
#ifdef SO_PEERCRED
    auto it = clients_set.find(name);
    if (it != clients_set.end()) {
           pid_t pid = it->second->pid;
        if (pid > 0) {
            if (kill(pid, SIGUSR1) == 0) {
                DPRINTF("Sent SIGUSR1 to client %s (pid %d)\n", name.c_str(), pid);
            } else {
                perror("kill");
            }
        } else {
            std::fprintf(stderr, "Client %s has invalid pid %d\n", name.c_str(), pid);
        }
    } else {
        std::fprintf(stderr, "Client %s not found for interrupt\n", name.c_str());
    }
#else
    std::assert(false && "interrupt_client: SO_PEERCRED not available on this platform");
#endif
}

// Parse raw buffer into Message. Wire format: 1 byte command followed by payload.
bool convert_data_to_message(const uint8_t* data, size_t length, Message& msg)
{
    if (length < 1) {
        msg.command = INVALID;
        msg.data.clear();
        return false;
    }
    msg.command = static_cast<COMMAND>(data[0]);
    if (length > 1) {
        msg.data.assign(data + 1, data + length);
    } else {
        msg.data.clear();
    }
    return true;
}

// Serialize Message into raw buffer (allocated with new[]). Caller receives ownership
// of `data` and must delete[] it when done.
void convert_message_to_data(const Message& msg, size_t& out_length, uint8_t*& data) {
    // Framed format: [uint32_t length (network order)] [1-byte command] [payload]
    uint32_t payload_len = static_cast<uint32_t>(1 + msg.length());
    out_length = 4 + payload_len;
    if (data != nullptr) {
        delete[] data;
    }
    data = new uint8_t[out_length];
    uint32_t net_len = htonl(payload_len);
    std::memcpy(data, &net_len, 4);
    data[4] = static_cast<uint8_t>(msg.command);
    if (msg.length() > 0) {
        std::memcpy(data + 5, msg.data.data(), msg.length());
    }
}

// Try to extract a single Message from the head of `buf`. If a complete framed
// message exists, remove it from buf and populate out_msg. Returns true on
// success, false if not enough bytes are yet available.
bool try_extract_message(std::vector<uint8_t>& buf, Message& out_msg) {
    if (buf.size() < 4) return false;
    uint32_t net_len = 0;
    std::memcpy(&net_len, buf.data(), 4);
    uint32_t payload_len = ntohl(net_len);
    if (payload_len == 0) return false; // treat as invalid
    if (buf.size() < 4 + payload_len) return false; // wait for more data

    // payload_len >= 1 (command + optional payload)
    uint8_t command = buf[4];
    out_msg.command = static_cast<COMMAND>(command);
    if (payload_len > 1) {
        out_msg.data.assign(buf.begin() + 5, buf.begin() + 4 + payload_len);
    } else {
        out_msg.data.clear();
    }

    // consume bytes
    buf.erase(buf.begin(), buf.begin() + 4 + payload_len);
    return true;
}

void initialize_client_name(std::shared_ptr<ClientConnection> client) {
    std::string name;
    if (!client->message.data.empty()) {
        name.assign(reinterpret_cast<const char*>(client->message.data.data()), client->message.length());
    }
    client->name = name;
    clients_set[name] = client;
    DPRINTF("Client initialized with name: %s\n", name.c_str());
}

void ask_for_compute(std::shared_ptr<ClientConnection> client) {
    std::string client_name = client->name;
    auto it_map = simulation_connections.find(client_name);
    if (it_map == simulation_connections.end()) {
        std::fprintf(stderr, "ask_for_compute: No simulation target for %s\n", client_name.c_str());
        return;
    }
    std::string targeted_client_name = it_map->second;
    auto it = clients_set.find(targeted_client_name);
    if (it == clients_set.end()) {
        std::fprintf(stderr, "ask_for_compute: Targeted client %s not found for client %s\n", targeted_client_name.c_str(), client_name.c_str());
        return;
    }
    auto targeted_client = it->second;
    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    if (targeted_client->message.command == COMPUTE_DATA_READY) {
        targeted_client->message.command = COMPUTE_FINISH;
        convert_message_to_data(targeted_client->message, out_len, out_buf);
        bridge_server_send_message(client->fd, out_buf, out_len);
        delete[] out_buf;
        DPRINTF("ask_for_compute: Sent COMPUTE_FINISH to client %s as data is ready from targeted client %s\n", client_name.c_str(), targeted_client_name.c_str());
        return;
    }
    // Forward the compute request to the targeted client
    interrupt_client(targeted_client_name);
    DPRINTF("ask_for_compute: Forwarded compute request from client %s to targeted client %s\n", client_name.c_str(), targeted_client_name.c_str());
    convert_message_to_data(client->message, out_len, out_buf);
    bridge_server_send_message(targeted_client->fd, out_buf, out_len);
    delete[] out_buf;
}

void compute_finish(std::shared_ptr<ClientConnection> client) {
    std::string client_name = client->name;
    auto it_map = simulation_connections.find(client_name);
    if (it_map == simulation_connections.end()) {
        std::fprintf(stderr, "compute_finish: No simulation target for %s\n", client_name.c_str());
        return;
    }
    std::string targeted_client_name = it_map->second;
    auto it = clients_set.find(targeted_client_name);
    if (it == clients_set.end()) {
        std::fprintf(stderr, "compute_finish: Targeted client %s not found for client %s\n", targeted_client_name.c_str(), client_name.c_str());
        return;
    }
    auto targeted_client = it->second;
    // Forward the compute finish message to the targeted client
    uint8_t* out_buf = nullptr; size_t out_len = 0;
    convert_message_to_data(client->message, out_len, out_buf);
    bridge_server_send_message(targeted_client->fd, out_buf, out_len);
    delete[] out_buf;
    DPRINTF("compute_finish: Forwarded compute finish from client %s to targeted client %s\n", client_name.c_str(), targeted_client_name.c_str());
    Message response_msg;
    response_msg.command = ASK_FOR_SCHEDULE_STOP;
    response_msg.data.clear();
    convert_message_to_data(response_msg, out_len, out_buf);
    bridge_server_send_message(client->fd, out_buf, out_len);
    delete[] out_buf;
    DPRINTF("compute_finish: Sent ASK_FOR_SCHEDULE_STOP to client %s\n", client_name.c_str());
}

void compute_in_progress(std::shared_ptr<ClientConnection> client) {
    std::string client_name = client->name;
    auto it_map = simulation_connections.find(client_name);
    if (it_map == simulation_connections.end()) {
        std::fprintf(stderr, "compute_in_progress: No simulation target for %s\n", client_name.c_str());
        return;
    }
    std::string targeted_client_name = it_map->second;
    auto it = clients_set.find(targeted_client_name);
    if (it == clients_set.end()) {
        std::fprintf(stderr, "compute_in_progress: Targeted client %s not found for client %s\n", targeted_client_name.c_str(), client_name.c_str());
        return;
    }
    auto targeted_client = it->second;
    uint8_t* out_buf = nullptr; size_t out_len = 0;
    convert_message_to_data(client->message, out_len, out_buf);
    bridge_server_send_message(targeted_client->fd, out_buf, out_len);
    delete[] out_buf;
    DPRINTF("compute_in_progress: Forwarded compute in progress from client %s to targeted client %s\n", client_name.c_str(), targeted_client_name.c_str());
}

void compute_data_ready() {
    // this is a notification from arch simulation, no action needed here
}

void ask_for_schedule_stop() {
    // this is a response to compute_finish, no action needed here
}

void wait_for_robot_signal() {
    // this is a request from the arch simulation, no action needed here
}

int setup_bridge_server_socket() {
    unlink(BRIDGE_SOCKET_PATH);
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fatal_error("socket");
    }

    struct sockaddr_un addr {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BRIDGE_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fatal_error("bind");
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        fatal_error("listen");
    }
    set_nonblocking(listen_fd);

    DPRINTF("Bridge server listening on %s\n", BRIDGE_SOCKET_PATH);
    return listen_fd;
}

void accept_new_client(int listen_fd, std::vector<pollfd>& poll_fds, int& next_client_id) {
    int client_fd = accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
        perror("accept");
        return;
    }
    set_nonblocking(client_fd);
    auto conn = std::make_shared<ClientConnection>();
    conn->fd = client_fd;
    conn->id = next_client_id++;
    conn->pid = get_peer_pid(client_fd);
    conn->name = "";
    conn->message.command = INVALID;
    clients[client_fd] = conn;
    poll_fds.push_back({client_fd, POLLIN, 0});
    WARNING_MSG("New client connected: %d\n", client_fd);
}

void run_bridge_server_loop(int listen_fd) {
    std::vector<pollfd> poll_fds;
    poll_fds.push_back({listen_fd, POLLIN, 0});
    int next_client_id = 1;

    // Debug: dump configured simulation connections
    DPRINTF("Simulation connections mapping:\n");
    for (const auto &p : simulation_connections) {
        DPRINTF("  %s -> %s\n", p.first.c_str(), p.second.c_str());
    }

    while (true) {
        int ret = poll(poll_fds.data(), poll_fds.size(), -1);
        if (ret < 0) {
            if (errno == EINTR) {
                DPRINTF("poll interrupted by signal\n");
                // Return to caller so higher-level code (e.g. Python) can handle
                // the signal (KeyboardInterrupt). Avoid fatal exit on EINTR.
                return;
            }
            fatal_error("poll");
        }

        for (size_t i = 0; i < poll_fds.size(); ++i) {
            if (poll_fds[i].revents & POLLIN) {
                if (poll_fds[i].fd == listen_fd) {
                    // Accept new client connection
                    accept_new_client(listen_fd, poll_fds, next_client_id);
                } else {
                    // Handle client data
                    int client_fd = poll_fds[i].fd;
                    char buffer[1024];
                    ssize_t bytes_read = read(client_fd, buffer, 
                                                            sizeof(buffer));
                    if (bytes_read <= 0) {
                        if (bytes_read < 0 && errno != EAGAIN) {
                            perror("read");
                        }
                        close(client_fd);
                        clients.erase(client_fd);
                        poll_fds.erase(poll_fds.begin() + i);
                        --i;
                        WARNING_MSG("Client disconnected: %d\n", client_fd);
                    } else {
                        auto itc = clients.find(client_fd);
                        if (itc == clients.end()) {
                            std::fprintf(stderr, "No client record for fd %d\n", client_fd);
                            continue;
                        }
                        auto conn = itc->second;
                        // Append received bytes to per-connection buffer
                        conn->recv_buffer.insert(conn->recv_buffer.end(), reinterpret_cast<uint8_t*>(buffer), reinterpret_cast<uint8_t*>(buffer) + bytes_read);
                        DPRINTF("Received %zd bytes from client %d, buffered=%zu\n", bytes_read, client_fd, conn->recv_buffer.size());

                        // Extract as many framed messages as present
                        Message parsed;
                        while (try_extract_message(conn->recv_buffer, parsed)) {
                            // Debug: print raw payload info
                            DPRINTF("Parsed message: cmd=%d payload_len=%zu payload='", parsed.command, parsed.length());
                            if (!parsed.data.empty()) {
                                // print as readable string (may contain non-printables)
                                std::string s(parsed.data.begin(), parsed.data.end());
                                DPRINTF("%s", s.c_str());
                            }
                            DPRINTF("'\n");
                            // Store parsed message in connection and dispatch
                            conn->message = parsed;
                            WARNING_MSG("Dispatching command=%d from client %d\n", conn->message.command, client_fd);
                            switch (conn->message.command) {
                                case INITIALIZE:
                                    initialize_client_name(conn);
                                    break;
                                case ASK_FOR_COMPUTE:
                                    ask_for_compute(conn);
                                    break;
                                case COMPUTE_FINISH:
                                    compute_finish(conn);
                                    break;
                                case COMPUTE_IN_PROGRESS:
                                    compute_in_progress(conn);
                                    break;
                                case COMPUTE_DATA_READY:
                                    compute_data_ready();
                                    break;
                                case ASK_FOR_SCHEDULE_STOP:
                                    ask_for_schedule_stop();
                                    break;
                                case WAIT_FOR_ROBOT_SIGNAL:
                                    wait_for_robot_signal();
                                    break;
                                default:
                                    std::fprintf(stderr, "Unknown command %d from client %d\n", conn->message.command, client_fd);
                                    break;
                            }
                        } // end while try_extract_message
                    } // end else (bytes_read>0)
                } // end else (not listen_fd)
            } // if (poll_fds[i].revents & POLLIN)
        } // for poll_fds
    } // while(true)
} // run_bridge_server_loop

int setup_bridge_client(std::string client_name) {
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        fatal_error("socket");
    }

    struct sockaddr_un addr {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BRIDGE_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fatal_error("connect");
    }

    WARNING_MSG("Connected to bridge server at %s\n", BRIDGE_SOCKET_PATH);
    // Send INITIALIZE message with client name as payload
    Message init_msg;
    init_msg.command = INITIALIZE;
    init_msg.data.assign(client_name.begin(), client_name.end());
    size_t init_len = 0; uint8_t* init_buf = nullptr;
    convert_message_to_data(init_msg, init_len, init_buf);
    ssize_t bytes_sent = write(client_fd, init_buf, init_len);
    delete[] init_buf;
    if (bytes_sent < 0) {
        fatal_error("write");
    }
    DPRINTF("Sent %zd bytes to bridge server\n", bytes_sent);
    return client_fd;
}

Message bridge_client_send_and_wait_response(
    int client_fd, const uint8_t* message, size_t length, 
    int timeout_ms) {
    // Ensure the socket is non-blocking so reads don't block indefinitely.
    set_nonblocking(client_fd);

    ssize_t bytes_sent = write(client_fd, message, length);
    if (bytes_sent < 0) {
        fatal_error("write");
    }
    DPRINTF("Sent %zd bytes to bridge server, "
                                    "waiting for response...\n", bytes_sent);

    struct pollfd pfd;
    pfd.fd = client_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    Message response;
    std::vector<uint8_t> resp_buffer;
    while (true) {
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            if (errno == EINTR) {
                DPRINTF("poll interrupted by signal while waiting for response\n");
                // Return immediately so higher-level code (e.g. Python) can
                // handle the signal (KeyboardInterrupt). Avoid retrying here.
                return response;
            }
            fatal_error("poll");
        } else if (ret == 0) {
            // timeout
            if (timeout_ms >= 0) {
                WARNING_MSG("Poll timed out after %d ms\n", timeout_ms);
            }
            break;
        } else {
            if (pfd.revents & POLLIN) {
                char buf[1024];
                // Read available data (loop until EAGAIN)
                while (true) {
                    ssize_t n = read(client_fd, buf, sizeof(buf));
                    if (n > 0) {
                        resp_buffer.insert(resp_buffer.end(), 
                                        reinterpret_cast<uint8_t*>(buf), 
                                        reinterpret_cast<uint8_t*>(buf) + n);
                    } else if (n == 0) {
                        // peer closed connection
                        WARNING_MSG("Server closed connection\n");
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // no more data for now
                            break;
                        } else if (errno == EINTR) {
                            continue; // try read again
                        } else {
                            perror("read");
                            // return what we have (possibly empty)
                            return response;
                        }
                    }
                }
                // Try to parse a framed Message from the accumulated bytes.
                if (try_extract_message(resp_buffer, response)) {
                    DPRINTF("Received framed response cmd=%d payload_len=%zu\n", response.command, response.length());
                    return response;
                }
            }
            if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                WARNING_MSG(
                    "Socket error/hangup detected (revents=0x%x)\n", 
                                                                pfd.revents);
                break;
            }
        }
    }
    return response;
}


void setup_bridge_server() {
    int listen_fd = setup_bridge_server_socket();
    run_bridge_server_loop(listen_fd);
    // If run_bridge_server_loop ever returns, clean up.
    cleanup_bridge(listen_fd);
};

Message bridge_client_check_for_message(int client_fd) {
    // Ensure the socket is non-blocking so reads don't block indefinitely.
    set_nonblocking(client_fd);

    Message response;
    std::vector<uint8_t> resp_buffer;

    char buf[1024];
    // Read available data (loop until EAGAIN)
    while (true) {
        ssize_t n = read(client_fd, buf, sizeof(buf));
        if (n > 0) {
            resp_buffer.insert(resp_buffer.end(), 
                            reinterpret_cast<uint8_t*>(buf), 
                            reinterpret_cast<uint8_t*>(buf) + n);
        } else if (n == 0) {
            // peer closed connection
            WARNING_MSG("Server closed connection\n");
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // no more data for now
                break;
            } else if (errno == EINTR) {
                continue; // try read again
            } else {
                perror("read");
                // return what we have (possibly empty)
                return response;
            }
        }
    }
    // Try to parse a framed Message from the accumulated bytes.
    if (try_extract_message(resp_buffer, response)) {
        DPRINTF("Received framed message cmd=%d payload_len=%zu\n", response.command, response.length());
    }
    return response;
}
