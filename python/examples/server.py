#!/usr/bin/env python3
import sys
import os
import socket
import fcntl
import bridge._bridge as br


def main():
    server_name = sys.argv[1] if len(sys.argv) > 1 else "server1"
    print(f"Server starting with name: {server_name}")
    client_pid, conn_fd = br.bridge_setup_server(server_name)
    print(f"Server connected fd: {conn_fd}, client_pid placeholder: {client_pid}")

    # wrap the connected fd in a Python socket object for convenience
    s = socket.socket(fileno=conn_fd)
    s.setblocking(True)

    # read one message from client
    msg = br.bridge_wait_for_message(conn_fd, 5000)
    print(f"Server received command={msg.command}, data={msg.data}")

    # reply
    m = br.Message()
    m.command = br.COMMAND.COMPUTE_RESPONSE
    m.data = b"ack"
    br.bridge_send_message(conn_fd, m)

    s.close()
    print("Server exiting")


if __name__ == '__main__':
    main()
