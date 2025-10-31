## Bridge (Unix-domain socket helper)

This small C++ library provides a lightweight helper for building a bridge-style server and clients over Unix-domain sockets. It is intended for local IPC between a master process (simulation server) and multiple clients (simulated devices, worker processes, etc.).

This README shows the library purpose, the wire format, the public APIs in `bridge.hpp`, and how to build and run the examples and tests included in this folder.

## Goals

- Provide a small framed message format to send commands + payloads between peers.
- Simple helpers to set up a server, accept clients, and run a poll-based server loop.
- Client helper to send a request and wait for a framed response (polling). 
- Utility to obtain peer PID via SO_PEERCRED and send an interrupt (SIGUSR1) to a connected client.

## Message / wire format

All messages are length-prefixed (framed). The frame format is:

- 4 bytes: payload length in network byte order (uint32_t). This length is (1 + payload_size).
- 1 byte: command id (enum value declared in `bridge.hpp`).
- N bytes: payload (zero or more bytes), represented as `std::vector<uint8_t>` in the API.

This framing allows partial / combined reads to be handled correctly and supports variable-size payloads.

## Header API (quick summary)

See `bridge/bridge.hpp` for full declarations. Major public helpers are:

- Message struct: contains `uint8_t command` and `std::vector<uint8_t> payload`.
- setup_bridge_server_socket(...): create the server listening socket (returns fd).
- accept_new_client(...): accept a new client connection and return a `ClientConnection` pointer.
- run_bridge_server_loop(...): poll() loop that reads from client sockets into per-connection buffers and dispatches messages.
- setup_bridge_client(...): connect a client to the server socket path.
- bridge_client_send_and_wait_response(...): sends a Message from a client and polls until a framed response is received; returns parsed Message.
- get_peer_pid(...): returns the PID of the remote peer using SO_PEERCRED (Linux-only).
- interrupt_client(...): send SIGUSR1 to the client peer using the stored PID.

For detailed function signatures and types, open `bridge/bridge.hpp`.

## Build (examples and tests)

The `bridge/` folder contains small Makefiles inside `examples/` and `tests/` that compile the library into a static archive (`libbridge.a`) and link example programs or unit tests against it.

From the repository root (or from `bridge/`) run:

```bash
# Build examples (creates libbridge.a and links examples)
cd bridge/examples
make

# Build tests
cd ../tests
make
```

Notes:
- The build uses the local `g++` (C++17). If your toolchain differs, open the Makefiles and adapt the compiler flags.
- The tests/examples assume a Linux environment (SO_PEERCRED is Linux-only) and an environment that allows creating AF_UNIX sockets in the file system.

## Running the examples (tiny tutorial)

The examples illustrate a simple server and a client. They are intentionally minimal and rely on the library's helpers.

1) Start the server

```bash
cd bridge/examples
./server
```

2) Start a client in another shell

```bash
cd bridge/examples
# some example client binary name - typically `client` in this folder
./client
```

The example server registers a set of known simulation connections and will forward certain commands between clients according to those mappings. The client example demonstrates sending a command using `bridge_client_send_and_wait_response` and printing the reply.

If you want to step through a small tutorial flow manually:

- Start the server in terminal A.
- In terminal B, run the `client` program which will connect and send an INITIALIZE message then perform a request/response.
- Inspect the server terminal output to see messages being forwarded between logical client names.

## Running unit & integration tests

After `make` inside `bridge/tests`, you will typically have three small test binaries, e.g.:

- `test_framing` — checks length-prefix framing roundtrip over socketpair.
- `test_socket_io` — sanity test for socket send/recv behavior.
- `test_integration` — small integration test that exercises the forwarding path and the interrupt (SIGUSR1) behavior.

Run each test directly from the `bridge/tests` directory:

```bash
cd bridge/tests
./test_framing
./test_socket_io
./test_integration
```

`test_integration` may fork child processes and will exercise the interrupt path by sending a signal to a registered client's PID. It is intended to run on a local development machine, not in restricted CI environments that block signals or process creation.

## Example usage in code

Typical client flow (pseudo-code):

```cpp
#include "bridge.hpp"

int fd = setup_bridge_client(socket_path);
Message req;
req.command = COMMAND::ASK_FOR_COMPUTE; // use the enum from bridge.hpp
req.payload = std::vector<uint8_t>{ /*your payload bytes*/ };

Message resp = bridge_client_send_and_wait_response(fd, req);
// inspect resp.command and resp.payload
```

Typical server loop flow (sketch):

```cpp
int listen_fd = setup_bridge_server_socket(socket_path);
// accept clients and build maps of ClientConnection shared_ptrs
run_bridge_server_loop(listen_fd, /*maps and handlers*/);
```

See the example programs in `bridge/examples` for concrete usage.

## Wire-format compatibility & portability

- The library uses a 4-byte big-endian length prefix. If you need cross-platform compatibility with different endianness, keep this format in mind when writing clients in other languages.
- The `get_peer_pid` and `interrupt_client` helpers use Linux-specific socket options (SO_PEERCRED); those will not work on macOS or other non-Linux OSes. You can guard calls or implement alternative out-of-band mechanisms for other platforms.

## Troubleshooting

- Permissions: make sure the process has permissions to create a Unix-domain socket under the configured path.
- Address already in use: remove previous socket files (e.g., `/tmp/bridge.sock` or similar) before starting the server.
- Tests failing on CI: some CI runners restrict signals or forks; run tests locally on Linux for full coverage.

## Next steps / contributions

- Add a graceful shutdown API for the server loop (a control pipe or an atomic stop flag).
- Improve error handling and return rich error codes instead of exiting on fatal errors.
- Add a C API wrapper if you need to interop with other languages.

If you want, I can also:

- Add a top-level `CMakeLists.txt` or unify builds under the project build system.
- Add a small README snippet inside `bridge/examples` that documents the concrete example command-line arguments.

## License

This README does not change the code's license. Consult project LICENSE files in the repository root.

---

If you'd like, I can: (a) add a short `bridge/examples/README.md` that explains exactly how the shipped `server` and `client` binaries behave and what arguments they expect, or (b) add a graceful stop API and update the integration test to use it. Which would you prefer next?
