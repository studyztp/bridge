#include "../bridge.hpp"
#include <thread>
#include <chrono>
#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

using namespace std::chrono_literals;

int main() {
    // configure mapping used by server
    simulation_connections["A"] = "B";
    simulation_connections["B"] = "A";

    // start server loop in background thread
    int listen_fd = setup_bridge_server_socket();
    std::thread server_thread([listen_fd](){ run_bridge_server_loop(listen_fd); });

    // give server a moment to start
    std::this_thread::sleep_for(200ms);

    // Create client B and send COMPUTE_DATA_READY
    int fdB = setup_bridge_client("B");
    Message ready;
    ready.command = COMPUTE_DATA_READY;
    ready.data.clear();
    size_t lenB = 0; uint8_t* bufB = nullptr;
    convert_message_to_data(ready, lenB, bufB);
    ssize_t wB = write(fdB, bufB, lenB);
    assert(wB == (ssize_t)lenB);
    delete[] bufB;

    // Create client A and send ASK_FOR_COMPUTE and wait for response
    int fdA = setup_bridge_client("A");
    Message ask;
    ask.command = ASK_FOR_COMPUTE;
    ask.data.clear();
    size_t lenA = 0; uint8_t* bufA = nullptr;
    convert_message_to_data(ask, lenA, bufA);

    Message resp = bridge_client_send_and_wait_response(fdA, bufA, lenA, 2000);
    delete[] bufA;

    if (resp.command != COMPUTE_FINISH) {
        std::cerr << "Integration test forwarding failed: expected COMPUTE_FINISH, got " << resp.command << "\n";
        // cleanup
        close(fdA); close(fdB);
        // attempt to stop server thread (not implemented); just exit with failure
        return 1;
    }
    std::cout << "forwarding: PASS\n";

    // Now test interrupt: fork a child that registers a name and waits for SIGUSR1
    pid_t pid = fork();
    if (pid == 0) {
        // child
        volatile sig_atomic_t got = 0;
        struct sigaction sa;
        sa.sa_handler = [](int){ /* nothing, just wake */ };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, nullptr);
        // connect and register name 'child'
        int fd = setup_bridge_client("child");
        // pause until signal
        pause();
        // on signal, exit 0
        _exit(0);
    } else if (pid > 0) {
        // parent: give child time to connect and register
        std::this_thread::sleep_for(200ms);
        // instruct server to send SIGUSR1 to client named 'child'
        interrupt_client("child");
        // wait for child to exit
        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        if (w != pid) {
            std::cerr << "waitpid failed\n";
            return 2;
        }
        if (WIFEXITED(status)) {
            std::cout << "interrupt: child exited with " << WEXITSTATUS(status) << "\n";
            if (WEXITSTATUS(status) != 0) return 3;
        } else if (WIFSIGNALED(status)) {
            std::cerr << "child killed by signal " << WTERMSIG(status) << "\n";
            return 4;
        }
    } else {
        perror("fork");
        return 5;
    }

    std::cout << "interrupt: PASS\n";

    // cleanup: close client fds
    close(fdA); close(fdB);

    // Stop server: no clean stop implemented; terminate process
    // We'll detach the server thread and exit leaving server; in tests this is acceptable.
    server_thread.detach();
    return 0;
}
