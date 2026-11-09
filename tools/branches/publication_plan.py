#!/usr/bin/env python3
"""Offline main-only candidate report. Publication is unconditionally disabled."""

import argparse
import json
import os
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
import distribution as generator

REPOSITORY = 'Pepeuch/universal-gnss'
ALLOWED_REF = 'refs/heads/main'


def candidate(context, source_sha, main_sha, request_publication=False):
    generator.require(not request_publication, 'publication is disabled in lot 3')
    generator.require(context.get('event_name') == 'workflow_dispatch', 'unauthorized event context')
    generator.require(context.get('repository') == REPOSITORY, 'unauthorized repository/fork')
    generator.require(context.get('ref') == ALLOWED_REF, 'source ref must be main')
    generator.require(re.fullmatch(r'[0-9a-f]{40}', source_sha or '') is not None,
                      'source must be a full immutable commit SHA')
    generator.require(source_sha == main_sha, 'source SHA does not equal the authorized main tip')
    return {'repository': REPOSITORY, 'authorized_ref': ALLOWED_REF, 'source_commit': source_sha,
            'publication_enabled': False, 'status': 'candidate_only_no_validation_or_write_authority'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--request-publication', action='store_true', help='Always rejected; negative safety check')
    args = parser.parse_args(argv)
    try:
        # The guard runs before looking at a repository or any potential credentials.
        generator.require(not args.request_publication, 'publication is disabled in lot 3')
        context = {'event_name': os.environ.get('GITHUB_EVENT_NAME'),
                   'repository': os.environ.get('GITHUB_REPOSITORY'), 'ref': os.environ.get('GITHUB_REF')}
        # Read-only, offline, local tracking reference. A future publisher must
        # independently authenticate and refresh main; this report cannot do that.
        main_sha = generator.Snapshot(args.repo, 'refs/remotes/origin/main').commit
        print(json.dumps(candidate(context, args.source_sha, main_sha), sort_keys=True))
    except (generator.DistributionError, OSError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
