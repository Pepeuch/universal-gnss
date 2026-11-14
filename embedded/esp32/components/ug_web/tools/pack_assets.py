"""Deterministically compress the three public Web assets; never package private files."""
# SPDX-License-Identifier: LGPL-3.0-only
import argparse
import gzip
import io
from pathlib import Path

ASSETS = ('index.html', 'app.js', 'style.css')


def encode(data):
    result = io.BytesIO()
    # No source path/name, timestamp or platform-specific OS byte in the header.
    with gzip.GzipFile(fileobj=result, mode='wb', filename='', mtime=0, compresslevel=9) as output:
        output.write(data)
    return result.getvalue()


def pack(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError('generated assets must be separate from the source assets')
    data = {name: encode((source / name).read_bytes()) for name in ASSETS}
    output.mkdir(parents=True, exist_ok=True)
    for name, content in data.items():
        (output / (name + '.gz')).write_bytes(content)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    pack(args.source, args.output)
