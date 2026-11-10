#!/usr/bin/env python3
"""Validate/reproduce one local distribution; optionally require an isolated build."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
import distribution as generator
import consumer_validation

TOOL = Path(__file__).resolve().parent


def isolated_build(distro, source, image):
    subprocess.run(['bash', str(TOOL / 'test_isolated.sh'), distro, str(source), image], check=True)


def validate(repo, ref, profile, options=(), build_image=None, *, manifest_bytes=None, builder=None,
             installed_consumer=False, consumer_builder=None):
    generator.require(not installed_consumer or (profile == 'ros2-lyrical' and 'mavros' in options
                      and bool(build_image and build_image.strip())),
                      'installed consumer requires a Lyrical MAVROS build image')
    snapshot = generator.Snapshot(repo, ref)
    # CI takes the manifest from the same immutable source SHA, not the worktree.
    manifest_bytes = manifest_bytes if manifest_bytes is not None else snapshot.read('tools/branches/manifest.yaml')
    with tempfile.TemporaryDirectory(prefix='ug-distribution-ci-') as directory:
        first, second = Path(directory) / 'first', Path(directory) / 'second'
        expected = generator.plan(snapshot, manifest_bytes, profile, options)
        generator.generate(snapshot, expected, first)
        generator.verify(first, expected)
        independent = generator.plan(snapshot, manifest_bytes, profile, options)
        generator.generate(snapshot, independent, second)
        generator.verify(second, independent)
        generator.require(generator.inspect_output(first) == generator.inspect_output(second),
                          'distribution is not deterministic')
        metadata = generator.decode((first / generator.METADATA).read_bytes())
        generator.require(metadata['source_commit'] == snapshot.commit
                          and metadata['distribution'] == profile
                          and metadata['options'] == sorted(options), 'source/profile metadata mismatch')
        consumer_report = 'not_requested'
        if installed_consumer:
            consumer_report = (consumer_builder or consumer_validation.validate)(metadata, first, build_image)
        elif build_image is not None:
            generator.require(bool(build_image.strip()), 'empty build image is not a build validation')
            (builder or isolated_build)(metadata['ros_distro'], first, build_image)
        # A build runner must not mutate the streamed source tree on the host.
        generator.verify(first, expected)
        return {'source_commit': snapshot.commit, 'distribution': profile, 'options': sorted(options),
                'manifest_sha256': metadata['manifest_sha256'],
                'inventory_sha256': metadata['inventory_sha256'], 'files': len(expected),
                'reproducibility': 'pass', 'integrity': 'pass',
                'isolated_build_and_tests': 'pass' if build_image is not None else 'not_requested',
                'installed_consumer': consumer_report,
                'publication_enabled': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=TOOL.parents[1])
    parser.add_argument('--source-ref', required=True)
    parser.add_argument('--distribution', choices=['ros2-kilted', 'ros2-lyrical'], required=True)
    parser.add_argument('--with', action='append', default=[], dest='options')
    parser.add_argument('--build-image', help='Require build AND tests using a pre-provisioned cached image')
    parser.add_argument('--installed-consumer', action='store_true',
                        help='Reuse a Lyrical MAVROS build and require a fresh installed consumer')
    args = parser.parse_args(argv)
    try:
        report = validate(args.repo, args.source_ref, args.distribution, args.options, args.build_image,
                          installed_consumer=args.installed_consumer)
    except (generator.DistributionError, subprocess.SubprocessError, OSError, ValueError, KeyError) as error:
        print(f'ERROR: distribution validation failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
