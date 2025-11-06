#!/usr/bin/env python3
import sys
import os
import socket
import bridge._bridge as br


def main():
    server_name = sys.argv[1] if len(sys.argv) > 1 else "server1"
    print(f"Server starting with name: {server_name}")
    client_pid, listen_fd = br.bridge_setup_server(server_name)
    print(f"Server listen fd: {listen_fd}, client_pid placeholder: {client_pid}")

    # wrap the listening fd in a Python socket and accept one connection
    s = socket.fromfd(listen_fd, socket.AF_UNIX, socket.SOCK_STREAM)
    # make sure blocking for example simplicity
    s.setblocking(True)
    conn, _ = s.accept()
    conn_fd = conn.fileno()
    print(f"Accepted connection fd: {conn_fd}")

    # read one message from client
    msg = br.bridge_wait_for_message(conn_fd, 5000)
    print(f"Server received command={msg.command}, data={msg.data}")

    # reply
    m = br.Message()
    m.command = br.COMMAND.COMPUTE_RESPONSE
    m.data = b"ack"
    br.bridge_send_message(conn_fd, m)

    conn.close()
    s.close()
    print("Server exiting")


if __name__ == '__main__':
    main()
