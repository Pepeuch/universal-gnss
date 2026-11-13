#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from esp32-rtk-gateway 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85; see ../licenses/legacy-GPL-3.0.txt.
"""Read a raw Linux serial port and reject noise, incomplete or corrupt RTCM3."""
import argparse
import json
import math
import os
import select
import sys
import termios
import time
import tty


def crc24q(data):
    crc = 0
    for byte in data:
        crc ^= byte << 16
        for _ in range(8):
            crc <<= 1
            if crc & 0x1000000:
                crc ^= 0x1864CFB
    return crc & 0xFFFFFF


class RtcmReader:
    def __init__(self):
        self.pending = bytearray()

    def feed(self, data):
        self.pending.extend(data)
        frames = []
        while self.pending:
            if self.pending[0] != 0xD3:
                raise ValueError('non-RTCM byte: possible boot/application log pollution')
            if len(self.pending) < 3:
                break
            if self.pending[1] & 0xFC:
                raise ValueError('invalid RTCM reserved header bits')
            size = ((self.pending[1] & 3) << 8 | self.pending[2]) + 6
            if len(self.pending) < size:
                break
            frame = bytes(self.pending[:size])
            if crc24q(frame[:-3]) != int.from_bytes(frame[-3:], 'big'):
                raise ValueError('RTCM CRC24Q mismatch')
            del self.pending[:size]
            frames.append(frame)
        return frames


def read_port(path, *, frames=10, timeout=30.0, output=None):
    if frames <= 0 or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('frames and timeout must be positive')
    parser = RtcmReader()
    count = total = 0
    started = time.monotonic()
    deadline = started + timeout
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    original = None
    try:
        original = termios.tcgetattr(fd)
        tty.setraw(fd, termios.TCSANOW)
        while count < frames:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'RTCM timeout: {count}/{frames} frames, {len(parser.pending)} pending bytes')
            ready, _, _ = select.select([fd], [], [], remaining)
            if not ready:
                continue
            data = os.read(fd, 4096)
            if not data:
                raise OSError('serial port disconnected or EOF')
            for frame in parser.feed(data):
                count += 1
                total += len(frame)
                if output is not None:
                    output.write(frame)
        # A continuous stream may begin its next frame in this last read.
        elapsed = time.monotonic() - started
        return {'frames': count, 'bytes': total, 'crc24q': 'pass', 'noise_bytes': 0,
                'pending_bytes': len(parser.pending), 'elapsed_seconds': elapsed,
                'bytes_per_second': total / elapsed if elapsed else 0,
                'frames_per_second': count / elapsed if elapsed else 0}
    finally:
        try:
            if original is not None:
                termios.tcsetattr(fd, termios.TCSANOW, original)
        finally:
            os.close(fd)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('port')
    ap.add_argument('--frames', type=int, default=10)
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('--output', help='Optional binary capture of validated frames')
    args = ap.parse_args(argv)
    try:
        if args.output:
            capture_fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(capture_fd, 'wb') as output:
                result = read_port(args.port, frames=args.frames, timeout=args.timeout, output=output)
        else:
            result = read_port(args.port, frames=args.frames, timeout=args.timeout)
    except (ValueError, OSError, TimeoutError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
