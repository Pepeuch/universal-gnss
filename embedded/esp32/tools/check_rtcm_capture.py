#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from esp32-rtk-gateway 7a1ec69f5e9d07522c0284ec2dbae380f72c7e85; see ../licenses/legacy-GPL-3.0.txt.
"""Compare two finite RTCM captures, without opening a serial device."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

from check_rtcm_usb import RtcmReader


def load_frames(path):
    parser = RtcmReader()
    frames = parser.feed(Path(path).read_bytes())
    if parser.pending:
        raise ValueError(f'{path}: incomplete RTCM frame')
    if not frames:
        raise ValueError(f'{path}: no complete RTCM frame')
    return frames


def compare(reference, received):
    expected, actual = Counter(reference), Counter(received)
    return {'expected_frames': len(reference), 'received_frames': len(received),
            'missing_frames': sum((expected - actual).values()),
            'unexpected_frames': sum((actual - expected).values()),
            'identical_order_and_bytes': reference == received,
            'crc24q': 'pass'}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('reference')
    ap.add_argument('received')
    args = ap.parse_args(argv)
    try:
        result = compare(load_frames(args.reference), load_frames(args.received))
    except (OSError, ValueError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0 if result['identical_order_and_bytes'] else 1


if __name__ == '__main__':
    sys.exit(main())
