#!/usr/bin/env python3
"""Prints a gateway's data stream to stdout, like `nc`, on any OS.

Windows has no netcat, so use this to feed tools/decode:

    python tools/stream.py 192.168.4.1 1456 | tools/decode/decode.exe
    python tools/stream.py --udp 1457 | tools/decode/decode.exe
"""
import argparse
import socket
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", nargs="?", help="gateway address (TCP)")
    p.add_argument("port", type=int, nargs="?", default=1456)
    p.add_argument("--udp", type=int, metavar="PORT", help="listen for UDP broadcasts on PORT instead")
    args = p.parse_args()
    out = sys.stdout.buffer
    try:
        if args.udp:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("", args.udp))
            while True:
                out.write(s.recv(2048))
                out.flush()
        if not args.host:
            p.error("give a host and port, or --udp PORT")
        with socket.create_connection((args.host, args.port), timeout=10) as s:
            s.settimeout(None)
            while True:
                data = s.recv(4096)
                if not data:
                    break
                out.write(data)
                out.flush()
    except (KeyboardInterrupt, BrokenPipeError):
        pass
    except OSError as e:  # refused, timed out, reset by the gateway
        sys.exit("stream: %s" % e)


if __name__ == "__main__":
    main()
