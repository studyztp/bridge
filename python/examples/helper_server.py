#!/usr/bin/env python3
import sys
import os
import time
import subprocess
import pathlib

import bridge._bridge as br


def main():
    # mapping of client_name -> server_name
    client_to_server = {
        "client1": "server1",
    }

    listen_fd = br.bridge_setup_helper_server_socket()
    print(f"Helper listening on fd {listen_fd}")

    # start server and client subprocesses (they will connect to the helper)
    base = pathlib.Path(__file__).parent
    server_py = str(base / "server.py")
    client_py = str(base / "client.py")

    server_proc = subprocess.Popen([sys.executable, server_py, "server1"]) 
    # small delay so server registers shortly after starting
    time.sleep(0.1)
    client_proc = subprocess.Popen([sys.executable, client_py, "client1"]) 

    print("Started server and client; entering helper loop")
    try:
        br.bridge_helper_server_loop(listen_fd, client_to_server)
    finally:
        br.bridge_close_helper_server_socket(listen_fd)

    # wait for children
    rc1 = server_proc.wait(timeout=5)
    rc2 = client_proc.wait(timeout=5)
    print(f"server exit: {rc1}, client exit: {rc2}")


if __name__ == '__main__':
    main()
