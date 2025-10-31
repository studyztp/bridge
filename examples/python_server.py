#!/usr/bin/env python3
import os
import sys
# Make the local `bridge/python` package importable without installing it.
# This uses a path relative to this script so it works regardless of the
# active Python interpreter (system python or virtualenv).
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'python')))
from bridge import _bridge as b

def main():
    # mirror the C++ example: A <-> B
    b.sim_set("A", "B")
    b.sim_set("B", "A")

    print("Starting Python bridge server socket...")
    listen_fd = b.setup_bridge_server_socket()
    if listen_fd < 0:
        print("Failed to create server socket")
        return 1
    print(f"Listening on fd {listen_fd}; entering server loop (blocking)")
    # This will block and run the server loop until process exit
    b.run_bridge_server_loop(listen_fd)
    return 0

if __name__ == '__main__':
    sys.exit(main())
