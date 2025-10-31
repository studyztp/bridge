#include "../bridge.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    // test that bridge_server_send_message and bridge_client_send_message
    // write the given bytes to the socket
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        return 2;
    }

    // explicit bytes: 1-byte command (0x01) + payload 'A','B','C'
    uint8_t test_bytes[4] = { 0x01, (uint8_t)'A', (uint8_t)'B', (uint8_t)'C' };
    // debug: print the bytes we're about to write
    std::cerr << "writing bytes:";
    for (int i = 0; i < 4; ++i) {
        unsigned char uc = static_cast<unsigned char>(test_bytes[i]);
        char tmp[8]; std::snprintf(tmp, sizeof(tmp), " %02x", uc);
        std::cerr << tmp;
    }
    std::cerr << "\n";
    ssize_t wrote = write(sv[0], test_bytes, 4);
    std::cerr << "write returned " << wrote << "\n";
    assert(wrote == 4);

    char buf[8];
    ssize_t r = read(sv[1], buf, sizeof(buf));
    assert(r >= 0);
    std::cerr << "read bytes=" << r << "\n";
    // print bytes in hex for debugging
    std::cerr << "data hex:";
    for (ssize_t i = 0; i < r; ++i) {
        unsigned char uc = static_cast<unsigned char>(buf[i]);
        char tmp[8];
        std::snprintf(tmp, sizeof(tmp), " %02x", uc);
        std::cerr << tmp;
    }
    std::cerr << "\n";
    if (r < 4) {
        std::cerr << "unexpected read length\n";
        return 3;
    }
    // basic sanity
    if (static_cast<unsigned char>(buf[0]) != 0x01) {
        std::cerr << "first byte mismatch: got " << std::hex << (static_cast<int>(static_cast<unsigned char>(buf[0]))) << std::dec << "\n";
        return 4;
    }
    std::string payload(buf+1, buf+4);
    if (payload != "ABC") {
        std::cerr << "payload mismatch: '" << payload << "'\n";
        return 5;
    }

    close(sv[0]); close(sv[1]);
    std::cout << "test_socket_io: PASS\n";
    return 0;
}
