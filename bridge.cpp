#include "bridge.hpp"

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        std::perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::perror("fcntl F_SETFL");
        return -1;
    }
    return 0;
}

int setup_server_socket(const std::string& socket_path) {
    unlink(socket_path.c_str());
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::perror("socket");
        return -1;
    }

    struct sockaddr_un addr{0};
    addr.sun_family = AF_UNIX;
    std::strncpy(
                addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("bind");
        close(listen_fd);
        return -1;
    }
    if (listen(listen_fd, SOMAXCONN) < 0) {
        std::perror("listen");
        close(listen_fd);
        return -1;
    }
    if (set_nonblocking(listen_fd) < 0) {
        close(listen_fd);
        return -1;
    }
    DPRINTF("Server socket setup at %s\n", socket_path.c_str());
    return listen_fd;
}

pid_t get_fd_pid(int fd) {
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

int bridge_setup_helper_server_socket() {
    return setup_server_socket(BRIDGE_SETUP_SOCKET_PATH);
}

int bridge_close_helper_server_socket(int listen_fd) {
    close(listen_fd);
    unlink(BRIDGE_SETUP_SOCKET_PATH);
    return 0;
}

int bridge_helper_server_loop(int listen_fd, 
    std::unordered_map<std::string, std::string> client_to_server_match) {
    std::unordered_map<std::string, pid_t> server_to_socket_set;
    std::unordered_map<std::string, int> waiting_clients;
    std::vector<pollfd> poll_fds;
    poll_fds.push_back({listen_fd, POLLIN, 0});
    int ret, client_fd;
    while (!client_to_server_match.empty()) {
        ret = poll(poll_fds.data(), poll_fds.size(), -1);
        if (ret < 0) {
            if (errno == EINTR) {
                WARNING_MSG("poll interrupted by signal\n");
                return -1;
            }
            std::perror("poll");
            return -1;
        }
        for (size_t i = 0; i < poll_fds.size(); ++i) {
            if (!(poll_fds[i].revents & POLLIN))
                continue;

            // If the listen fd has activity, accept a new connection.
            if (poll_fds[i].fd == listen_fd) {
                int new_fd = accept(listen_fd, nullptr, nullptr);
                if (new_fd < 0) {
                    std::perror("accept");
                    continue;
                }
                set_nonblocking(new_fd);
                poll_fds.push_back({new_fd, POLLIN, 0});
                WARNING_MSG("Accepted new connection: %d\n", new_fd);
                continue;
            }

            // Otherwise it's activity on a client/helper connection.
            client_fd = poll_fds[i].fd;
            char buffer[MAX_BUFFER_SIZE];
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

            if (bytes_read <= 0) {
                if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::perror("read");
                }
                close(client_fd);
                poll_fds.erase(poll_fds.begin() + i);
                WARNING_MSG("Closed connection: %d\n", client_fd);
                --i;
                continue;
            }

            Message parsed;
            if (!convert_data_to_message(reinterpret_cast<uint8_t*>(buffer),
                                         static_cast<size_t>(bytes_read),
                                         parsed)) {
                WARNING_MSG("Failed to parse message from fd %d\n", client_fd);
                continue;
            }
            DPRINTF("Received command %d from fd %d\n", parsed.command, client_fd);
            switch (parsed.command) {
                case INITIALIZE_SERVER: {
                    std::string server_name =
                        convert_bytes_to_string(parsed.data.data(), parsed.length());
                    pid_t pid = get_fd_pid(client_fd);
                    server_to_socket_set[server_name] = pid;
                    DPRINTF("Registered server %s with pid %d\n", server_name.c_str(), pid);
                    if (!waiting_clients.empty()) {
                        for (auto it = waiting_clients.begin(); it != waiting_clients.end(); ++it) {
                            const std::string& client_name = it->first;
                            auto expected_it = client_to_server_match.find(client_name);
                            if (expected_it != client_to_server_match.end() && expected_it->second == server_name) {
                                // Send the server pid to the waiting client
                                Message response;
                                response.command = INITIALIZE_SERVER;
                                response.data.resize(sizeof(pid_t));
                                std::memcpy(response.data.data(), &pid, sizeof(pid_t));
                                bridge_send_message(it->second, response);
                                DPRINTF("Sent server pid %d to client %s\n", pid, client_name.c_str());
                                waiting_clients.erase(it);
                                client_to_server_match.erase(client_name);
                                break;
                            }
                        }
                    }
                    break;
                }
                case INITIALIZE_CLIENT: {
                    std::string client_name =
                        convert_bytes_to_string(parsed.data.data(), parsed.length());
                    auto expected_it = client_to_server_match.find(client_name);
                    if (expected_it == client_to_server_match.end()) {
                        WARNING_MSG("Received initialize client for unknown name %s\n", client_name.c_str());
                        break;
                    }
                    std::string expected_server = expected_it->second;
                    auto server_it = server_to_socket_set.find(expected_server);
                    if (server_it != server_to_socket_set.end()) {
                        // server is already registered, send pid immediately
                        Message response;
                        response.command = INITIALIZE_SERVER;
                        response.data.resize(sizeof(pid_t));
                        pid_t server_pid = server_it->second;
                        std::memcpy(response.data.data(), &server_pid, sizeof(pid_t));
                        bridge_send_message(client_fd, response);
                        DPRINTF("Sent server pid %d to client %s\n", server_pid, client_name.c_str());
                        client_to_server_match.erase(client_name);
                    } else {
                        // server not registered yet, add to waiting list
                        waiting_clients[client_name] = client_fd;
                        DPRINTF("Client %s is waiting for server %s\n", client_name.c_str(), expected_server.c_str());
                    }
                    break;
                }
                default: {
                    WARNING_MSG("Unknown command %d received\n", parsed.command);
                    break;
                }
            }
        }
    }

    return 0;

}

int bridge_setup_client(
                std::string client_name, pid_t& server_pid, int& socket_fd) {
    // First send INITIALIZE_CLIENT message with client_name
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::perror("socket");
        return -1;
    }
    struct sockaddr_un addr{0};
    addr.sun_family = AF_UNIX;
    std::strncpy(
        addr.sun_path, BRIDGE_SETUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("connect");
        close(socket_fd);
        return -1;
    }
    WARNING_MSG("Client %s connected to bridge helper socket\n", 
                                                        client_name.c_str());
    Message msg;
    msg.command = INITIALIZE_CLIENT;
    msg.data.assign(client_name.begin(), client_name.end());
    Message response;
    if (bridge_send_and_wait_for_response(socket_fd, msg, response, -1) < 0) {
        WARNING_MSG("Failed to get response during client setup\n");
        close(socket_fd);
        return -1;
    }
    if (response.command != INITIALIZE_SERVER || response.length() != sizeof(pid_t)) {
        WARNING_MSG("Invalid response received during client setup\n");
        close(socket_fd);
        return -1;
    }
    close(socket_fd);
    char target_socket_path[MAX_BUFFER_SIZE];
    sprintf(target_socket_path, "/tmp/sim_socket_%d.sock", *reinterpret_cast<pid_t*>(response.data.data()));
    server_pid = *reinterpret_cast<pid_t*>(response.data.data());
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::perror("socket");
        return -1;
    }
    addr = {0};
    addr.sun_family = AF_UNIX;
    std::strncpy(
        addr.sun_path, target_socket_path, sizeof(addr.sun_path) - 1);
    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("connect");
        close(socket_fd);
        return -1;
    }
    
    WARNING_MSG("Client %s connected to server socket %s\n", 
        client_name.c_str(), target_socket_path);
    
    return 0;
}

int bridge_setup_server(std::string server_name, pid_t& client_pid, 
                                                            int& socket_fd) {
    // First send INITIALIZE_SERVER message with server_name
    int helper_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (helper_socket_fd < 0) {
        std::perror("socket");
        return -1;
    }
    struct sockaddr_un addr{0};
    addr.sun_family = AF_UNIX;
    std::strncpy(
        addr.sun_path, BRIDGE_SETUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(helper_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("connect");
        close(helper_socket_fd);
        return -1;
    }
    WARNING_MSG("Server %s connected to bridge helper socket\n", 
                                                        server_name.c_str());
    Message msg;
    msg.command = INITIALIZE_SERVER;
    msg.data.assign(
        server_name.begin(), server_name.end()
    );

    // Now setup the actual server socket
    std::string server_socket_path =
        "/tmp/sim_socket_" + std::to_string(getpid()) + ".sock";

    socket_fd = setup_server_socket(server_socket_path);
    if (socket_fd < 0) {
        return -1;
    }
    WARNING_MSG("Server %s setup socket at %s\n", 
        server_name.c_str(), server_socket_path.c_str());

    // Send the INITIALIZE_SERVER message to the helper
    if (bridge_send_message(helper_socket_fd, msg) < 0) {
        WARNING_MSG("Failed to send INITIALIZE_SERVER message\n");
        close(helper_socket_fd);
        return -1;
    }

    // Close the helper socket after sending the message
    close(helper_socket_fd);

    return 0;
}

int bridge_send_and_wait_for_response(int socket_fd, 
        const Message &msg_to_send, Message& response_msg, int timeout_ms) {
    // Send the message
    if (bridge_send_message(socket_fd, msg_to_send) < 0) {
        WARNING_MSG("Failed to send message\n");
        return -1;
    }

    // Wait for response
    Message response = bridge_wait_for_message(socket_fd, timeout_ms);
    if (response.command == INVALID) {
        WARNING_MSG("Failed to receive response message\n");
        return -1;
    }
    // Just copy the response
    // This way because don't want to depend on copy constructor because 
    // local variables might be deleted at function exit
    response_msg.command = response.command;
    response_msg.data.assign(
        response.data.begin(), response.data.end()
    );
    return 0;
}

Message bridge_wait_for_message(int socket_fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = socket_fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        std::perror("poll");
        return Message();
    } else if (ret == 0) {
        WARNING_MSG("Timeout waiting for message\n");
        return Message();
    }

    uint8_t buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read = read(socket_fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            std::perror("read");
        } else {
            WARNING_MSG("Connection closed while waiting for message\n");
        }
        return Message();
    }

    Message received_msg;
    if (!convert_data_to_message(buffer, bytes_read, received_msg)) {
        WARNING_MSG("Failed to parse received message\n");
        return Message();
    }
    return received_msg;
}

int bridge_send_message(int socket_fd, const Message &msg) {
    size_t data_length;
    uint8_t* data_buffer = nullptr;
    convert_message_to_data(msg, data_length, data_buffer);
    ssize_t total_sent = 0;
    while (static_cast<size_t>(total_sent) < data_length) {
        ssize_t bytes_sent = write(socket_fd, data_buffer + total_sent, data_length - total_sent);
        if (bytes_sent < 0) {
            if (errno == EINTR)
                continue;
            std::perror("write");
            delete[] data_buffer;
            return -1;
        }
        total_sent += bytes_sent;
    }
    delete[] data_buffer;
    return 0;
}

int bridge_send_interrupt(pid_t target) {
    if (kill(target, SIGUSR1) < 0) {
        std::perror("kill");
        return -1;
    }
    return 0;
}

bool convert_data_to_message(
                            const uint8_t* data, size_t length, Message& msg) {
    if (length < sizeof(COMMAND)) {
        return false;
    }
    msg.command = *reinterpret_cast<const COMMAND*>(data);
    msg.data.resize(length - sizeof(COMMAND));
    std::memcpy(msg.data.data(), data + sizeof(COMMAND), 
                                                    length - sizeof(COMMAND));
    return true;
}

void convert_message_to_data(const Message& msg, size_t& out_length, 
                                                            uint8_t*& data) {
    out_length = sizeof(COMMAND) + msg.length();
    data = new uint8_t[out_length];
    std::memcpy(data, &msg.command, sizeof(COMMAND));
    if (msg.length() > 0) {
        std::memcpy(data + sizeof(COMMAND), msg.data.data(), msg.length());
    }
}

std::string convert_bytes_to_string(const uint8_t* bytes, size_t length) {
    return std::string(reinterpret_cast<const char*>(bytes), length);
}
