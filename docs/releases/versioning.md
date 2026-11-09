# Version policy after v0.7.4

## Functional versions and repository structure

Universal GNSS versioning follows functional changes and compatibility rules.
The maintainer determines whether a functional change requires a maintenance,
feature or compatibility release. Moving files, reorganizing Git branches or
generating specialized branches does not automatically require a new functional
UG version. Structural changes still require validation of affected builds,
packages and consumers. This documentation change updates no version number.

The maintainer considers **v0.7.4 functionally stable and validated**. That
acceptance is distinct from the existence of a tag, published images or a GitHub
Release, and does not close outstanding validation checklist items. The
[rc6 campaign](../validation/2026-11-08-rc6-passive-mowing.md) retains its exact
historical identity and bounded evidence.

At the repository audit and synchronization baseline
`ba7f220da30ee0b216f199ab24947db0678b3d7b`, no exact `v0.7.4` tag or GitHub
Release was observed; functional acceptance is not a claim of publication.
Record any later publication separately with the actual tag/source SHA, image
digests, verification results and Release URL. Do not rename historical
measurements or infer missing artifact qualification from maintainer acceptance.

## Branch ownership

[Contributions](../../CONTRIBUTING.md) target `dev`, the complete development
repository. `main` remains the complete stable repository and receives only
validated changes from `dev`. Specialized distribution branches are generated
exclusively from an identified commit on `main`; they receive neither manual
development nor contribution PRs. Fix bugs through `dev`, validate for `main`,
then regenerate the applicable distributions.

The [repository refactor plan](../../TODO_REPO_REFACTOR.md) defines this policy.
The old main-only integration instructions in the
[v0.7.4 publication preparation](v0.7.4-publication.md) and its shared handoff
record that historical operation; they do not override the current contribution
policy. `dev` and `main` were synchronized at the baseline above. There is no
standing instruction to create separate maintenance/development lines; any
future exception requires an explicit policy decision and documented integration
rules that keep common fixes in the complete development repository.

## Functional milestones

The existing series below describe functional release planning. Repository
refactoring has its own lots and does not advance these milestones by itself.

| Series or first milestone | Purpose / entry criterion |
| --- | --- |
| `v0.7.x` | Stable maintenance: scoped fixes, compatibility and security work. |
| `v0.8.0-alpha.1` | Optional experimental development when early publication is useful. |
| `v0.8.0-beta.1` | First beta when new features are sufficiently advanced for broader testing. |
| `v0.8.0-rc.1` | Feature freeze; final regression, deployment and physical qualification. |
| `v0.8.0` | Next stable after explicit maintainer acceptance of its release evidence. |

Keep maintenance fixes separate from experimental feature changes even when
both are developed through `dev`; never promote experimental changes into a
maintenance release through an unreviewed bulk integration. If that separation
requires additional long-lived branches, decide their policy before using them.

Increment the numeric prerelease suffix for later candidates. Published tags
are immutable identities, not promises of production readiness. Keep stable
and prerelease image references explicit; deployments should pin verified
immutable digests rather than silently follow an experimental tag.

## Engine, distribution and publication qualification

Engine qualification covers common GNSS behavior, protocols, public contracts
and applicable deterministic or physical evidence. Distribution qualification
covers the selected engine SHA plus platform dependencies, packaging,
installation, build, tests and update/rollback behavior. Neither qualification
automatically establishes the other, and a directory move awards no new hardware
or release-gate evidence.

A generated distribution must identify its source `main` commit, UG functional
version and generator identity. Distribution/generator metadata can evolve
without automatically changing the UG functional version. A future ESP32
BASE / RELAY / ROVER and ESP-NOW LR integration is a functional priority with
no release number assigned by this policy; Linux, BlueOS and ROS2 Humble work
remain in the [plan](../../TODO_REPO_REFACTOR.md).

Publication is a separate authorized operation after validation. The current
[Docker workflow](../../.github/workflows/docker-release.yml) matches **every
`v*` tag**, including alpha, beta and RC tags, and automatically publishes
images on tag push. It does not wait for other CI workflows or create a GitHub
Release. Require successful applicable CI on the exact approved source SHA
before tag/image publication; record the actual artifacts afterward.

The future generated-branch process is: validated `dev` changes → `main` →
manifest generation and verification → per-distribution build/tests → authorized
publication. Generator implementation, protections, tag creation, image pushes
and GitHub Release creation are outside this documentation PR.
