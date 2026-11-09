# Contributing

## Branches and pull requests

Create a work branch from an up-to-date `dev` and target pull requests at `dev`.
`dev` contains the complete development repository. `main` contains the complete
stable repository and receives only reviewed, validated changes from `dev`.
Do not target contribution PRs at `main` or at generated distribution branches.

Specialized branches, when implemented, are generated exclusively from an
identified commit on `main`. They are distribution outputs, never development
branches or PR targets. Fix a backend bug on a work branch from `dev`, add the
applicable regression evidence, integrate through `dev`, validate for `main`,
then regenerate the affected distributions. Implement a common correction once
in the owning shared component so every applicable backend benefits.

Generated-branch tooling, its publication workflow and protection configuration
are future work. This policy does not claim that they are already enforced. See the
[repository plan](TODO_REPO_REFACTOR.md) and [version policy](docs/releases/versioning.md).

## Validation and public contracts

Run the tests applicable to the affected component and its consumers. A change
may enter `main` only after the applicable build, test, formatting and review
checks pass. For documentation changes, check consistency, relative links and
`git diff --check`; run the status generator in check mode if planning text is
affected. Follow [AGENTS.md](AGENTS.md) for repository execution rules.

Preserve existing package and library names, include paths, exported targets,
executables, ROS interfaces, launch/configuration contracts and runtime semantics.
An intentional public compatibility change requires explicit justification,
documentation and approval. Preserve coordinate precision, protocol behavior,
time/freshness/provenance contracts and historical validation evidence.

A structural move must update its build/test/install/documentation references
in the same PR, preserve behavior and have an independent rollback. Do not
combine repository rearrangement with engine changes or automatically bump a
functional version. Engine qualification, distribution qualification and
publication are separate decisions; a successful backend build alone does not
qualify every distribution.

## Design rules

- Keep gnss_core independent from ROS 2.
- Do not add vendor-specific hacks to the public runtime model.
- Map vendor data into capability-driven common structures.
- Keep parsing, transport, receiver configuration, and ROS 2 publishing separated.
- Add tests for every parser and runtime mapper.
- Keep Universal GNSS hardware-agnostic; downstream integrations do not define core behavior.

## New vendor support should include

- protocol documentation reference
- parser implementation
- runtime mapping
- capability list
- sample logs or test vectors
- tests

## Publication

After validated integration into `main`, the future distribution pipeline will
generate from its exact source SHA, verify the manifest and dependencies, build
and test each distribution, then publish only successful outputs with source
metadata. Generated branches must prohibit manual development; setting up the
protections and authorized publishing identity is a separate operation.

Tags, images and GitHub Releases require their own publication decision. The
[current Docker release workflow](.github/workflows/docker-release.yml) already
publishes images on every `v*` tag, including prereleases, without waiting for
other workflows. Require successful applicable CI on the exact approved SHA
before an authorized tag push. This document implements no publication action.

## Use conventional commits

- feat:
- fix:
- docs:
- test:
- refactor:
- chore:
