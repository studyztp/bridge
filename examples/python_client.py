#!/usr/bin/env python3
import os
import sys
import argparse
# Make the local `bridge/python` package importable without installing it.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'python')))
from bridge import _bridge as b

def main():
    p = argparse.ArgumentParser()
    p.add_argument('name', nargs='?', default='A', help='client name')
    p.add_argument('message', nargs='?', default='hello', help='payload (text)')
    p.add_argument('command', nargs='?', type=int, default=b.COMMAND.ASK_FOR_COMPUTE, help='command id')
    args = p.parse_args()

    fd = b.setup_bridge_client(args.name)
    print(f"Connected as {args.name}, fd={fd}")

    payload = args.message.encode('utf-8')
    print("Sending payload:", payload)
    msg = b.client_send_and_wait(fd, args.command, payload, -1)

    print("Got reply: command=", int(msg.command))
    print("payload bytes:", msg.data)
    try:
        print("payload as text:", msg.data.decode('utf-8'))
    except Exception:
        pass

    b.close_fd(fd)

if __name__ == '__main__':
    main()
