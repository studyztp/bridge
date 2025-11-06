#!/usr/bin/env python3
import sys
import os
import time
import bridge._bridge as br


def main():
    client_name = sys.argv[1] if len(sys.argv) > 1 else "client1"
    print(f"Client starting with name: {client_name}")
    server_pid, sock_fd = br.bridge_setup_client(client_name)
    print(f"Client connected to server pid: {server_pid}, fd: {sock_fd}")

    # send a compute request
    m = br.Message()
    m.command = br.COMMAND.COMPUTE_REQUEST
    m.data = b"hello"
    br.bridge_send_message(sock_fd, m)

    # wait for response
    resp = br.bridge_wait_for_message(sock_fd, 5000)
    print(f"Client received: command={resp.command}, data={resp.data}")

    # close socket
    try:
        os.close(sock_fd)
    except Exception:
        pass


if __name__ == '__main__':
    main()
