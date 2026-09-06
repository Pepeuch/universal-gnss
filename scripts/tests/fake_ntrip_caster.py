#!/usr/bin/env python3
"""Deterministic credential-free NTRIP caster used by Docker CI."""

from __future__ import annotations

import argparse
import socket
import time


def crc24q(data: bytes) -> int:
    polynomial = 0x1864CFB
    crc = 0
    for value in data:
        crc ^= value << 16
        for _ in range(8):
            crc <<= 1
            if crc & 0x1000000:
                crc ^= polynomial
            crc &= 0xFFFFFF
    return crc


def rtcm_frame(message_type: int = 1077) -> bytes:
    payload = bytes(((message_type >> 4) & 0xFF, (message_type & 0x0F) << 4))
    framed = bytes((0xD3, 0x00, len(payload))) + payload
    checksum = crc24q(framed)
    return framed + checksum.to_bytes(3, "big")


def serve(port: int) -> None:
    response = b"ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"
    frame = rtcm_frame()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", port))
        listener.listen()
        print(f"fake_ntrip_caster event=listening port={port}", flush=True)
        while True:
            connection, _ = listener.accept()
            with connection:
                request = bytearray()
                while b"\r\n\r\n" not in request and len(request) < 8192:
                    block = connection.recv(1024)
                    if not block:
                        break
                    request.extend(block)
                if not request.startswith(b"GET /RTCM3 "):
                    print("fake_ntrip_caster event=request_rejected", flush=True)
                    continue
                print("fake_ntrip_caster event=request_accepted", flush=True)
                try:
                    connection.sendall(response + frame + frame)
                    while True:
                        time.sleep(0.2)
                        connection.sendall(frame)
                except (BrokenPipeError, ConnectionResetError):
                    print("fake_ntrip_caster event=client_disconnected", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=2101)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be in the 1..65535 range")
    serve(args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
