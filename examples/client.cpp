#include <iostream>
#include <string>
#include "bridge.hpp"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: client <name> <message> <command>\n";
        return 1;
    }
    std::string name = argv[1];
    std::string message = argv[2];
    std::string command = argv[3];
    int fd = setup_bridge_client(name);
    Message msg;
    msg.command = (COMMAND)atoi(command.c_str());
    msg.data.assign(message.begin(), message.end());
    size_t out_len = message.length(); uint8_t* out_buf = nullptr;
    convert_message_to_data(msg, out_len, out_buf);
    if (!out_buf) {
        std::cerr << "failed to build message\n";
        return 1;
    }
    Message resp = bridge_client_send_and_wait_response(fd, out_buf, out_len, -1);
    delete[] out_buf;
    if (resp.command != INVALID) {
        std::cout << "Received reply: ";
        std::cout << convert_bytes_to_string(
            reinterpret_cast<const uint8_t*>(resp.data.data()), 
            resp.length()) << std::endl;
    } else {
        std::cout << "No reply (timeout or empty)" << std::endl;
    }
    close(fd);
    return 0;
}
