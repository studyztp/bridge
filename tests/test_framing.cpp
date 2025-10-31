#include "../bridge.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    // create a message
    Message m;
    m.command = ASK_FOR_COMPUTE;
    std::string payload = "hello-world";
    m.data.assign(payload.begin(), payload.end());

    // serialize (framed)
    size_t out_len = 0; uint8_t* out_buf = nullptr;
    convert_message_to_data(m, out_len, out_buf);
    assert(out_buf != nullptr && out_len > 0);

    // write to a socketpair and read it back to ensure framing survives
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        return 2;
    }

    ssize_t w = write(sv[0], out_buf, out_len);
    assert(w == (ssize_t)out_len);

    std::vector<uint8_t> buf;
    buf.resize(out_len);
    ssize_t r = read(sv[1], buf.data(), out_len);
    assert(r == (ssize_t)out_len);

    // parse using try_extract_message
    Message parsed;
    bool ok = try_extract_message(buf, parsed);
    if (!ok) {
        std::cerr << "try_extract_message failed\n";
        return 3;
    }
    assert(parsed.command == m.command);
    assert(parsed.length() == m.length());
    std::string parsed_s(parsed.data.begin(), parsed.data.end());
    assert(parsed_s == payload);

    close(sv[0]); close(sv[1]);
    delete[] out_buf;
    std::cout << "test_framing: PASS\n";
    return 0;
}
