# CI-04A — Validation events

The seven ordinary workflows (native CMake, ROS2 Lyrical, ROS2 Kilted,
distributions, Docker, formatting and backlog) run on `pull_request` and on
pushes to `dev` or `main` only. Pull requests retain their default activity
types, including opening and new commits, with no target-branch or path filter;
fork pull requests retain the same event and job configuration.

A push to a working branch does not start an additional push campaign. Open a
pull request to obtain automatic validation. Merging into `dev` or `main` still
starts validation of the resulting commit. A permanently open `dev` to `main`
pull request can still produce both a push campaign and a pull-request campaign
when `dev` changes; this policy does not promise absolute deduplication.

Only event filters change. Workflow/check names, jobs, matrices, permissions,
concurrency, build/test commands and the fail-closed `always()` distribution gate
are preserved. Ordinary validation no longer runs on tag pushes; the separate
tag/manual Docker release workflow, publication conditions and permissions are
unchanged. This policy grants no publication authority.

`tests/test_workflow_events.py` checks every ordinary workflow and rejects
unrestricted/working-branch pushes, missing stable branches, path/tag filters,
filtered pull-request activity and privileged alternative triggers. Existing
gate tests retain all sixteen prerequisite-result combinations. These local
checks validate configuration; actual GitHub event dispatch must be confirmed
on a later authorized push and pull request.

---

# CI-02 — Independent relocated installation

The Lyrical + MAVROS row now passes `--installed-consumer` to `validate_ci.py`.
It reuses that row's three-package build and tests; no extra matrix row or
Universal GNSS rebuild is added. The four Lyrical configurations and existing
Kilted row remain as described below. CI-01's original scope statement below
applies to CI-01 alone; CI-02 adds the installed consumer documented here.

The producer extends `test_isolated.sh` with an optional installation export.
After its mandatory tests succeed, the prefix and distribution source metadata
are copied out with Docker's API. The producer is removed before consumption.
The host verifies the exported metadata against the exact generated candidate
and resolves the dependency image to one immutable image ID for both containers.

A new offline, unprivileged container receives only the installed prefix archive,
source metadata, a synthetic NMEA fixture and the small tests under `consumer/`.
It receives no source/build workspace, source bind mount, socket, device or
credentials. Its environment is cleared, preinstalled UG packages are rejected,
and the original `/tmp/ug-work/{source,build,install}` and host workspace must be
absent. The prefix moves to `/tmp/ug-consumer/prefix`; no producer setup chain is
sourced, only declared ROS dependencies and the relocated local setup.

Mandatory checks from that installation:

- exact resolution of all three package prefixes and ament/CMake resources;
- canonical ROSIDL, Python type support and serialization of both messages and
  service request/response;
- external C++ compilation against public headers and exported ROS2/MAVROS targets;
- pluginlib discovery and actual factory loading from the relocated installed
  library, with a path identity assertion;
- ELF dependency resolution without producer RPATH/RUNPATH dependencies;
- all five installed launch descriptions with `--show-args`, plus resolution of
  the installed parameter file;
- bounded receiver/NTRIP start/stop using unreachable loopback TCP, and a synthetic
  stepped replay with a Trigger service call and actual canonical GnssStatus receipt.

Child processes are reaped with bounded SIGINT/TERM/KILL cleanup. Containers are
removed on success, failure and host timeout. Producer failure prevents consumer
execution. The consumer is limited to five minutes; the whole job retains its
existing 40-minute budget. Candidate SHA, image ID, archive hash, process logs,
commands/errors and elapsed time are printed to GitHub's job log. Temporary
exports are not publications and are deleted after validation.

Consumer failure/timeout fails the existing isolated matrix and therefore the
unchanged `always()` gate. Tests cover propagation, prerequisite results,
metadata mismatch and isolation/cleanup. After the positive consumer succeeds,
three additional fresh consumers must reject a missing prefix, CMake export and
plugin library with the expected diagnostic; arbitrary failures or timeouts do
not count as successful negative controls. No UG build is repeated. A future PR must
confirm actual GitHub execution; local results alone do not do so.

This qualifies standalone ament installation, not the incomplete native SDK or
integrated MAVROS index. Source/build strings in diagnostic binaries are allowed;
the test proves functional independence by making those paths unavailable.
No FCU, physical receiver, Compose, ARM or release qualification is added.

```bash
python3 tools/branches/validate_ci.py --source-ref HEAD \
  --distribution ros2-lyrical --with mavros \
  --build-image ug-distribution-build:lyrical-mavros --installed-consumer
```

The image is built with the CI-01 public dependency recipe below. The command is
run as the normal project user from the repository root. No publication is enabled.

---

# CI-01 — Lyrical distribution build coverage

The current workflow preserves the eight generation/integrity/reproducibility
combinations and the existing Kilted default build. Its isolated matrix contains
exactly these five jobs:

| ROS2 | Source options | Packages built/tested | Dependency image |
| --- | --- | --- | --- |
| Kilted | none | messages, ROS2 | existing dependency set |
| Lyrical | none | messages, ROS2 | no MAVROS |
| Lyrical | MAVROS | messages, ROS2, MAVROS | pinned MAVROS dependencies |
| Lyrical | Docker | messages, ROS2 | no MAVROS |
| Lyrical | MAVROS + Docker | messages, ROS2, MAVROS | pinned MAVROS dependencies |

Docker is a **source payload option**, not a production image build. The current
production Dockerfile does not select the MAVROS package. No new Kilted option
build, independent installed consumer (CI-02), Compose qualification (CI-03),
factory-loading qualification or hardware test is added here.

`Dockerfile.validation` defaults to `WITH_MAVROS=false`; `true` is accepted only
for Lyrical. The public ROS apt repository supplies these exact Debian artifacts:

- `ros-lyrical-mavros=2.16.0-1resolute.20260928.093732`
- `ros-lyrical-libmavconn=2.16.0-1resolute.20260928.090729`
- `ros-lyrical-mavros-msgs=2.16.0-1resolute.20260928.090809`
- `ros-lyrical-mavlink=2026.9.9-1resolute.20260909.091024`

These satisfy the adapter's unchanged `find_package(mavros 2.16.0 EXACT)`.
The [official Lyrical package status](https://repo.ros2.org/status_page/ros_lyrical_default.html)
identifies the MAVROS 2.16.0 release. Provisioning also installs Eigen3,
GeographicLib development files, diagnostic_updater, eigen3_cmake_module,
pluginlib, rclcpp_components, rcpputils and tf2_eigen/tf2_ros; the existing image
supplies the ROS2/test/SSL/build tools. Apt resolves the remaining declared
system/ROS dependencies. No geographic dataset is needed: these tests do not
start a MAVROS FCU node.

Missing pinned artifacts fail provisioning rather than selecting another MAVROS
version. Their availability and the image's floating base/transitive apt
packages remain operational dependencies: this is not a complete system
snapshot or a promise of byte-identical future images. Refreshing these pins
requires a reviewed dependency update and rerunning this matrix.

The workflow builds its images from this public recipe; it uses no private image,
local lot-13 digest, credentials or Universal GNSS overlay. The recipe contains
no COPY/ADD or project source build. The runner clears its environment, rejects
preinstalled Universal GNSS ament packages before building, and checks that
colcon discovers exactly the two or three selected packages. It retains normal
UID/GID, network isolation, tar/stdin transfer and no source/device/socket mounts.
MAVROS's adapter and RTCM encoder tests are included in the same mandatory
colcon test invocation; a failure in either cannot be filtered out.

The final gate still has `if: always()` and requires both generator and the
complete isolated matrix to report success. Tests execute its actual shell for
all sixteen combinations of success/failure/cancelled/skipped prerequisites;
only success/success is accepted. Additional regressions execute the actual
matrix option-forwarding shell, reject missing Lyrical coverage or extra Kilted
rows, and exercise the overlay rejection guard.

Cost: the isolated matrix grows from two to five jobs: three additional image
provisionings and build/test runs, all on Lyrical. Each retains its 40-minute
limit and `fail-fast: false`; aggregate timeout capacity rises from 80 to 200
job-minutes, not a wall-time estimate. Actual latency depends on package
availability, GitHub scheduling and CPU. No cache or job is allowed to hide a
failed prerequisite.

Local reproduction (normal project user, from repository root):

```bash
docker build --build-arg ROS_DISTRO=lyrical --build-arg WITH_MAVROS=true \
  --tag ug-distribution-build:lyrical-mavros \
  --file tools/branches/Dockerfile.validation tools/branches
python3 tools/branches/validate_ci.py --source-ref HEAD \
  --distribution ros2-lyrical --with mavros \
  --build-image ug-distribution-build:lyrical-mavros
```

For the no-MAVROS rows provision with `WITH_MAVROS=false`; Docker rows add
`--with docker`. Local tests do not establish GitHub execution or runner timing;
those must be checked on the eventual PR. No workflow or publication is triggered
by documenting these commands.

The following lot-3 design and validation record is historical and retained
verbatim. Its original two-job build coverage is superseded by the CI-01 matrix
above; its recorded results are not new CI-01 results.

---

# Lot 3 — Distribution CI and inactive publication design

Base: `eff7263629a5ec91e87879c7b2d8a058e80191cf`, the verified merge of PR 13
into `dev`. This lot adds validation; it publishes no distribution branch, tag,
image or GitHub Release and changes no GitHub protection setting. Existing CI,
GNSS sources, generator rules/manifest, public contracts, versions and historical
validation evidence remain unchanged.

## Validation architecture

[distributions.yml](../../.github/workflows/distributions.yml) runs on `push` and
`pull_request`, including fork PRs. There is no `pull_request_target`, privileged
workflow chain or publication trigger.

1. **Distribution generator and integrity** installs the YAML parser used by CI
   policy tests, then runs the 44 generator tests and the lot 3 orchestration/
   security tests. It generates/verifies/reproduces Kilted and Lyrical for each
   option combination: default, MAVROS, Docker, MAVROS+Docker. All dependency and
   classification checks remain those of the unmodified lot 2 generator.
2. **Isolated ROS2 kilted / lyrical** depends on the generator job. Each matrix
   job provisions a dependency-only image using
   [Dockerfile.validation](Dockerfile.validation), then invokes
   [validate_ci.py](validate_ci.py) with `--build-image`, making build and tests
   mandatory. The image contains no project sources or UG installation.
3. **Distribution validation gate** runs with `always()` and fails unless both
   the generator job and the complete isolated matrix succeeded. Failures,
   cancellations and skipped prerequisite jobs cannot become a green gate.

The source is the checkout's immutable `github.sha`. On a PR this can be GitHub's
synthetic merge SHA; on a push it is the pushed SHA. This is validation identity,
not an authorized publication source. The checkout includes Git history and uses
`persist-credentials: false`. No untrusted PR title/branch string enters a shell
command. Scripts receive the SHA through a quoted environment variable.

`validate_ci.py` reads the manifest from that same commit, generates two
independent trees, compares all bytes/modes, checks metadata against source SHA,
profile and options, and verifies integrity again after the optional build. A
missing dependency, incomplete/mutated output, non-deterministic result, bad
metadata, build failure or test failure exits nonzero. Build-only or test-only
success is insufficient. Temporary trees are removed on success and failure.

Generation uses no network. Dependency image provisioning uses public ROS images
and apt packages with network access; subsequent compilation/tests use the
unchanged [test_isolated.sh](test_isolated.sh), with network disabled, no host
source or Docker socket mounts, cleared environment, normal UID/GID and tar/stdin
transfer of only the generated tree. System dependency provisioning may run as
root; project build/test commands run as the normal project user.

MAVROS remains optional: all option closures are verified in CI, and the
isolated runner includes its package/tests whenever selected. The default
GitHub build matrix provisions Kilted/Lyrical **without MAVROS**. To exercise
MAVROS locally, supply an image with the exact declared MAVROS dependencies;
this lot does not silently skip its tests or declare every combination qualified.
The existing ROS2, native, Docker, format, backlog and release workflows are
preserved, including their existing permissions and triggers.

## Effective validation permissions

The new workflow declares only `contents: read`; jobs do not override it.
Other token permissions are unset. No publication secret, GitHub App token,
`GH_TOKEN`, OIDC write grant, package write grant, credential persistence,
registry login, remote write, artifact promotion or deployment environment is
configured. The automatic token used by checkout is read-only; it is not passed
to generated code or dependency containers. Fork approval requirements are GitHub
settings, not an excuse to expose a privileged token.

This follows GitHub's [minimum token permission guidance](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token).
The isolation claim concerns project sources and write authority, not hermetic
reproduction of floating public system dependencies. Pinning/refresh policy for
those dependency images remains an operational concern.

## Local commands

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/branches/tests -v
python3 tools/branches/validate_ci.py --source-ref HEAD --distribution ros2-kilted
python3 tools/branches/validate_ci.py --source-ref HEAD --distribution ros2-lyrical --with mavros

docker build --build-arg ROS_DISTRO=kilted --tag ug-distribution-build:kilted --file tools/branches/Dockerfile.validation tools/branches
python3 tools/branches/validate_ci.py --source-ref HEAD --distribution ros2-kilted --build-image ug-distribution-build:kilted
```

The CI policy tests require PyYAML; the workflow installs `python3-yaml` and uses
`/usr/bin/python3` for those tests. The generator and orchestration runtime still
need only the standard Python library, Git and Bash. A validation without
`--build-image` explicitly reports `not_requested` for build/tests; it cannot
stand in for the mandatory isolated CI job.

## Publication preparation — disabled

[publication_plan.py](publication_plan.py) is an offline **candidate report**, not
a publisher or authorization service. It always rejects `--request-publication`,
even with a matching main SHA and apparently valid GitHub context. No workflow
calls it. It accesses no credential and implements no remote write.

Its simulated future-context checks require the canonical repository
`Pepeuch/universal-gnss`, a manual `workflow_dispatch` context on
`refs/heads/main`, a full SHA and equality with the locally recorded `origin/main`
tip. PR/fork/dev/tag contexts, abbreviated or arbitrary/non-tip SHAs fail.
Environment values and a local tracking ref can be forged by a caller; a report
therefore states that it provides neither validation nor write authority. It
cannot establish remote freshness. No active manual publication trigger is
added by this lot.

A later activation PR must implement and review the following transaction:

1. Run only trusted workflow/tool code from protected `main` in the canonical
   repository, through an explicitly approved event/environment. No code/artifact
   from a PR, fork, arbitrary SHA or untrusted `workflow_run` may acquire a write
   token. Resolve authenticated remote `main` once to source SHA **S** and record
   that exact authorized ref/SHA. Reject an input SHA different from **S**.
2. Use only committed inputs at **S**, including generator and manifest. Validate
   all components/references, generate twice and build/test every distribution
   selected for this publication set. Inspect source SHA, generator/manifest/
   inventory hashes and options in each output. Fail the entire set if any
   mandatory check fails, is skipped or becomes unknown.
3. Produce a complete candidate-set record with source **S**, profile list,
   options, image identities and output hashes. The trusted writer must verify
   the set and its source binding itself; a green job name or editable artifact
   is insufficient. Bind human environment approval to this exact set.
4. Only after those checks, obtain a short-lived token for the dedicated publisher
   GitHub App on this repository. Recheck remote main authorization and the old
   distribution tips. Construct distribution commits with the previous tip as
   parent so replacement normally remains fast-forward. Serialize publication
   jobs and refuse concurrent/unexpected tip changes. Do not automatically force
   or delete a ref to resolve a conflict.
5. Permit only an explicit list of distribution destination refs. Reject `main`,
   `dev`, tags and every undeclared ref in the writer and at the server rules.
   Update the **whole selected set atomically**, or update none. A partially
   updated branch set followed by compensating pushes is not atomic publication.
6. Verify every resulting remote tip and metadata binding before recording the
   completed set. Preserve the prior set for a separately authorized rollback;
   never silently rewrite a released source identity or claim hardware evidence.

For multi-ref publication, [Git's atomic push contract](https://git-scm.com/docs/git-push)
is all-or-nothing and fails when unsupported. **GitHub atomic capability for this
repository has not been demonstrated.** Before activation, prove it and concurrent
update/protection rejection behavior on a disposable, separately authorized
repository with equivalent rules. If unsupported or incompatible with protections,
keep publication disabled and obtain approval for another coherent mechanism
(such as one activation pointer to an immutable complete set). Do not substitute
sequential branch updates, REST calls or a best-effort rollback.

The current automatic v-tag Docker image publisher is independent and unchanged;
this plan grants no tag/image/Release authorization.

## Future identity and protections

Use a dedicated GitHub App installation token, limited to this repository and
`contents: write` plus the platform's implicit metadata read permission. No
administration/workflow/package write grant is requested for this branch-only
writer. `contents: write` also authorizes other repository-content operations;
it is not a separate branch-only or Release-denial permission. The future
writer must expose no tag/Release operation, and server ref restrictions must
cover all tags and non-allowlisted heads as well as main/dev. The App credential
would live only in an approved protected publication environment, with trusted workflow code; validation jobs never receive it.
GitHub [installation tokens](https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/generating-an-installation-access-token-for-a-github-app)
can be narrowed by repository and permission and expire after one hour.

That token is **not branch-scoped**. Enforce branch restrictions on the server;
a YAML ref allowlist alone cannot prevent a compromised writer from changing
`main`/`dev`. GitHub [branch rulesets](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository)
support scoped branch targets and explicit GitHub App bypass actors. The App
must have bypass only for the generated-branch update restriction, never for
main/dev rules. A standard `GITHUB_TOKEN` with write permission is not assumed
to bypass a protected branch automatically.

Proposed settings, to be applied later with separate authorization:

| Target | Required policy |
| --- | --- |
| `dev` | PRs, review, existing mandatory checks plus Distribution validation gate; no direct publisher-App updates, deletion or force pushes |
| `main` | Controlled promotion of validated dev changes, review and exact-SHA checks; no direct publisher-App updates, deletion or force pushes |
| Explicit generated refs | Restrict creation/updates to the dedicated App; forbid manual contributions, deletion and force pushes; preserve audited source metadata |

Avoid broad role/admin bypass on generated refs. Add an independent ruleset that
keeps non-bypassable constraints (for example deletion/non-fast-forward bans)
separate from the App-only update restriction. Add catch-all rules for
non-allowlisted heads and tags that do not authorize the publisher App; do not assume that protecting only known generated refs
blocks it from creating another ref. For main/dev, require PR/review and deny
direct updates to the App without giving it a bypass. Verify the App
cannot modify protections or merge its own unreviewed change. A human emergency
path, if needed, is a separately audited repository-owner decision.

Rules configuration and App installation must be tested together: App tokens
must successfully update only authorized generated refs while attempts at
main/dev, tags, unknown refs and unauthorized users fail. Do not activate based
solely on this proposed configuration or a workflow file's presence.

## Protection audit — 2026-11-09, read-only

Observed [public branch metadata](https://api.github.com/repos/Pepeuch/universal-gnss/branches/dev)
and [rulesets](https://api.github.com/repos/Pepeuch/universal-gnss/rulesets) for this repository:

- `branches/main` and `branches/dev`: `protected: false`, enforcement off and no
  required status-check contexts.
- `rulesets`, `rules/branches/main` and `rules/branches/dev`: empty lists.
- Branch listing: no `ros2`, `humble`, `linux`, `esp32` or `blueos` generated branch.
- Detailed legacy `branches/main/protection` and `branches/dev/protection`: HTTP
  401 without API authentication. Their detailed settings were not inspected.

These are observations, not configured protections or a guarantee against future
changes. Re-audit authenticated effective rules, bypass actors, required check
sources, token scope, repository/environment settings and branch creation policy
before any activation. No GitHub setting was changed in this lot.

## Validation record and remaining gates

Local source baseline: `eff7263629a5ec91e87879c7b2d8a058e80191cf`. Results:

| Check | Local result |
| --- | --- |
| Existing generator tests + new CI/policy tests | 73 tests PASS (44 existing + 29 new) |
| Existing Python tooling suite | 55 tests PASS |
| Dependency-only images, exact new Dockerfile | Kilted and Lyrical builds PASS |
| New orchestrator, Kilted default pilot | Release build PASS; 165 tests, no errors/failures/skips |
| New orchestrator, Lyrical default pilot | Release build PASS; 166 tests, no errors/failures/skips |
| New orchestrator, Lyrical + optional MAVROS | Three packages built; 168 tests, no errors/failures/skips |
| All eight profile/option combinations | Generation, integrity and reproducibility PASS |

New image identities:
Kilted `sha256:0eb6163dd3943411718e8355430a319b86d1baca11d84a0930ef81b73e7ce8cf`;
Lyrical `sha256:2c2b6e77472c03a522de6442344aac64ad9a1df1723fd7b506e6a346627f9029`.
The optional MAVROS run reuses the lot 2 external dependency image
`sha256:8404b5a397f5eeeb98d673184cfc9748324a80672bf18086914aa71cafba12af`.
All project builds/tests run under the normal user with network disabled.

Unit fault injections cover invalid manifest/dependencies, incomplete or changed
outputs, non-determinism, metadata/source mismatch, build/test exit failures and
post-build mutation. Tests also exercise dirty-manifest isolation, denied PR/fork/
dev/tag/arbitrary-SHA publication contexts and the unconditional publication lock.
Workflow tests parse YAML and reject permission escalation, credential persistence,
privileged PR triggers, ignored failures or removal of the mandatory build gate.
These simulations do not establish real GitHub App/ruleset/atomic-push behavior.

GitHub Actions execution of this new workflow is pending a later approved push/PR;
local tests and dependency-image builds do not substitute for that check. Also
not executed: Kilted+MAVROS build, ARM, generated production Docker images,
hardware, authenticated publisher/protection trials or any publication. External
bootstrap availability and fork approval behavior must be confirmed on GitHub.

Activation remains blocked on protected main/dev and generated refs, a tested
publisher identity, authenticated effective-rule audit, exact-main publication
validation, a complete-set integrity/approval boundary, atomic-update proof and
explicit publication authorization. No unsupported backend, hardware, ARM,
registry artifact or official distribution publication is qualified by this lot.
