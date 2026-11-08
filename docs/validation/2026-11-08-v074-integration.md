# v0.7.4 documentary integration validation — 2026-11-08

Status: local preparation complete; remote integration/publication pending.
Baseline: rc6 and observed remote main
`d02e23f17d34e58f75f4c7a7f72f8d34ea766bfa`. The remote HEAD was rechecked
before local integration and again at final review; no v0.7.4 tag was present.
The approved candidate is commit `24d109d` (22 files); test infrastructure is
commit `4c2a3f4`. Subsequent stable preparation changes documentation/shared
handoffs only. Worktree: isolated Universal GNSS main, normal user ubuntu;
production submodule and parent gitlink remain at d02e23f.

## Results

| Check | Result / scope |
| --- | --- |
| Native Release configure/build/CTest | PASS, 68/68 tests, local x86_64. |
| ROS Lyrical Release colcon | PASS, 166 tests, zero errors/failures/skips, local x86_64. |
| Python tooling discovery | PASS, 55/55 including generators, workflow contracts, plot identities and library provenance. |
| Backlog generation followed by `--check` | PASS; release 61/65, project 88/194, UGA 33/205. |
| Shared checkpoint audit | PASS; 10 indexed checkpoints, no problems/warnings. |
| Four campaign SVGs + render manifest | PASS, byte-for-byte reproduction with recorded Python 3.14.4 / matplotlib 3.11.2. |
| Metrics/series/script/source/SVG hashes | PASS; approved report/metrics/series preserved. |
| Relative Markdown links/anchors | PASS, 82 targets across 31 candidate files; external endpoint availability is not an offline check. |
| Targeted credential/JSON/SVG checks | PASS; no raw NTRIP settings, credentials or large raw logs staged. |
| Python syntax / `git diff --check` | PASS, including complete diff against rc6. |
| Runtime equivalence | PASS for production sources, build statements, interfaces, launch, configuration and Docker inputs. |
| Remote CI on prepared commits | PENDING approved main push, then exact-SHA CI. |
| New local Kilted/arm64 or image build | NOT RUN; no production runtime change; require new CI/artifact qualification before publication. |
| Authenticated registry payload inspection | PENDING; anonymous rc6 Kilted inspection returned HTTP 403. |

The generated `docs/status/uga_backlog.svg` is intentionally unchanged:
SHA256 `5ab9e6e00e42273cb22497a82f087cd44a08626eb00252a292536e995c525080`.
TODO checklist lines and the canonical manifest retain their baseline state.

## Provenance regression: red, diagnosis, green

Initial Lyrical Release reported one failed test among 166: the provenance
checker required libntrip to be loaded by replay even though replay does not
call NTRIP and Release linking can omit that unused dependency. Real `ldd`
resolution proved replay covers driver/protocols/transport/tools and the NTRIP
consumer covers ntrip/protocols/transport from the current build. No stale
loaded library was observed.

The checker now examines both real consumers. All five expected DSOs must load
in at least one, and every observed copy must match the expected build path.
Three unit regressions cover collective loading, absence everywhere and a stale
copy masked by a correct copy elsewhere. The focused real Release test and full
166-test Lyrical suite are green. This preserves provenance protection rather
than dropping the NTRIP requirement or changing production linkage.

An intermediate manual CMake reconfiguration omitted the generated message
overlay, preventing eight suites from starting. A complete colcon forced
reconfigure with both ROS and generated-message overlays resolved that test
environment mistake. It required no runtime change or weakened assertion.

## Runtime-equivalence review

The complete diff from d02e23f contains only shared/docs, README/TODO/ROADMAP,
validation scripts/tests, backlog CI and ROS test infrastructure. The sole
production CMake file hunk is the provenance test registration within
`if(BUILD_TESTING)`. Its prefix and suffix outside that test block were compared
byte for byte with rc6. No production target, dependency, source, public message,
launch setting, Dockerfile or runtime entrypoint changed. No C++ formatting is
needed because no C++ file was touched. Unchanged runtime source and image recipe do
not imply identical rebuilt images: upstream ROS base images and apt inputs
are floating and must be recorded in actual publication provenance.

## Reproduction and handoff

Commands for normal-user validation, exact-main-SHA CI and artifact inspection
are in the [publication procedure](../releases/v0.7.4-publication.md).
The exact backlog workflow validation block passed locally (34 existing plus
10 plot/provenance tests), as did the full 55-test Python discovery suite. Native builds use CMake Release/CTest; ROS builds use colcon with
universal_gnss_msgs and universal_gnss_ros2, sourcing /opt/ros/lyrical and the
generated install overlay. Use a dedicated localhost-only ROS domain for tests.
Raw build/test outputs stay outside Git under the validation-build directory;
this document is the compact durable outcome, not a log archive.

The [rc6 field report](2026-11-08-rc6-passive-mowing.md) retains its exact
historical setup and limits. Endurance is accepted by maintainer decision from
operating feedback, with missing historical versions/durations/logs stated.
UGA-126 and persistence remain open under their canonical prerequisite plans;
no hardware acceptance is inferred from this integration or the 57-minute run.
The [shared release handoff](../../.agent/shared/RELEASE-V0.7.4.md) is ACTIVE;
its exact next step is final review, approved main push, exact-SHA CI, then
separate stable tag/image/Release approval. Preparation GO; immediate tag NO-GO.
