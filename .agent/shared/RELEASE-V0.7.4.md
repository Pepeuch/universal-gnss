# v0.7.4 stable — integration and publication handoff

Disposition: ACTIVE. Target branch: **main only**, per maintainer instruction.
Local preparation is authorized; no push, tag, image publication or GitHub
Release is authorized. Production checkout and MowgliNext gitlink stay unchanged.

## Decisions and remote baseline

- Maintainer selects v0.7.4 stable as successor to v0.7.4-rc6, retaining known
  validation limits rather than fabricating completion. Endurance is accepted
  by maintainer decision from operating feedback; no repeat campaign requested.
- Remote HEAD/main observed 2026-11-08: `d02e23f17d34e58f75f4c7a7f72f8d34ea766bfa`.
  Annotated rc6 tag object `0e1cf5e45a50e1675ebe6c930de39977d92dfdd1` peels to
  that exact commit. No v0.7.4 tag exists at observation time. Recheck before push.
- Remote dev `11e3e597c8f2743d0c260e8e19d9b3727119bc40` is behind main and lacks
  later runtime fixes. Do not use dev as the stable base or reset it silently.
- The 22 reviewed candidate files were integrated into local main as `24d109d`.
  Remote main has not moved; local integration is not remote integration.

## Evidence and invariant

Keep [the rc6 campaign handoff](RC6-20261108-REVIEW.md), its exact historical
SHA/image, report, compact data, four SVGs and render manifest. No old measured
baseline is renamed v0.7.4. TODO/manifest remain the sole accounting authority:
61/65 release, 88/194 project, 33/205 UGA until actual new qualifying evidence.

All production runtime source/build inputs must remain identical to rc6.
Test-only provenance infrastructure may be corrected separately with explicit
proof that production CMake statements and runtime files remain unchanged.
Stable OCI version/revision and fresh upstream dependencies can change image
digests; identical source does not imply byte-identical images or transferred
physical qualification.

## Validation observations — preserve failures as well as passes

- Local native Release build/CTest: 68/68 PASS.
- Full pre-change Python tooling suite: 51/51 PASS.
- rc6 GitHub Actions: 13 completed successful runs, including native GCC/Clang,
  Kilted/Lyrical, Docker smoke and the two multi-architecture publication jobs.
  These are rc6 results, not CI results for the new integration commits.
  Inspectable baseline runs include [release images](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435101),
  [Kilted](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435125),
  [Lyrical](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435118),
  [native](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435154),
  [backlog](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435157),
  [format](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435156),
  [Docker smoke](https://github.com/Pepeuch/universal-gnss/actions/runs/39339435213).
- Additional local Lyrical Release: 166 reported tests, one failure in
  `test_build_library_provenance`. The replay ELF does not load libntrip; the
  checker demanded all five DSOs from it. ldd with the test environment proves
  replay loads driver/protocols/transport/tools, while test_ntrip_node loads
  ntrip/protocols/transport. No stale-library load was observed. Preserve the
  identity invariant across both real consumers instead of dropping the NTRIP
  requirement. Test-only correction committed as `4c2a3f4`: the focused real
  Release provenance test is green, and final full Lyrical colcon reports
  **166 tests, 0 errors, 0 failures, 0 skipped**. Three new unit regressions
  preserve missing-library rejection and reject a stale copy even if another
  consumer resolves correctly. Full Python tooling suite: **55/55 PASS**.
  An intermediate manual CMake reconfigure omitted the generated messages
  overlay and prevented eight suites from starting. Reconfiguring via colcon
  with the complete overlay fixed that validation-environment error; no runtime
  change or test relaxation was needed.
- GHCR anonymous inspection of rc6 Kilted was refused 403. Publication CI success
  and `sbom:true`/`provenance:mode=max` are not independent inspection of the
  stored artifacts. Authenticated per-platform digest/SBOM/provenance checks
  remain required; no credential was requested/stored and nothing was pushed.

## Remaining acceptance / publication boundary

UGA-126 retired-response cutoff and profile persistence remain hardware-required;
the 57-minute session does not close them. Publication indexes/SBOM/provenance
must be checked after the separately approved tag push. Endurance acceptance is
a declared maintainer decision with incomplete version/duration/log records,
not newly acquired instrumented rc6 evidence; counters remain unchanged.

Push of any v* tag automatically starts the GHCR-publishing workflow. Approval
to create a local tag is not by itself permission to push it/publish images.
Require green CI on the exact integrated main SHA before any approved tag push;
the publishing workflow is not dependent on all other workflows finishing.

## Next step and handoff lifecycle

The approved 22-file candidate and test-only correction are committed locally
on main. Stable notes, version policy and the publication runbook are prepared:
[release notes](../../docs/releases/v0.7.4.md),
[procedure](../../docs/releases/v0.7.4-publication.md),
[versioning](../../docs/releases/versioning.md). The campaign handoff is RETAINED
for its bounded evidence; current release state stays here. Final remote-main
integration needs approval, then CI on that exact SHA, then separate explicit
tag/image/Release authorization. The independent clone's origin points to the
local production repository: never push there; use the explicit GitHub URL in
the runbook. No branch, tag or image was pushed.
Keep this release handoff ACTIVE until tag/image/GitHub Release actions are
explicitly authorized and their post-publication evidence is recorded. Never
mark release artifacts published based on preparation alone.

Durable software validation: [integration report](../../docs/validation/2026-11-08-v074-integration.md).

## Final review checks

Final offline review: 31 candidate files, 82 relative Markdown targets,
JSON/SVG/credential checks PASS; exact Backlog Status CI validation block PASS
(34 existing plus 10 plot/provenance tests), shared audit 10 checkpoints with
zero problems/warnings, generator/check and complete rc6 diff whitespace PASS.
The four campaign SVGs and render manifest are unchanged from the approved
candidate and reproduced byte for byte; their exact source/script/series/content hashes are checked. The
status SVG is intentionally unchanged because accounting is unchanged:
`5ab9e6e00e42273cb22497a82f087cd44a08626eb00252a292536e995c525080`.
Production runtime files and Docker inputs match rc6; only the BUILD_TESTING
CMake region and test helper differ outside documentary/tooling paths.

Historical endurance/physical matrices remain as recorded. Native Kilted/arm64
and fresh multi-architecture image builds were not run locally for this
integration; rc6 Actions results retain their exact SHA, and new remote CI is
pending approval to push main. No hardware validation is claimed here.

Technical preparation: GO for final integration review. Immediate stable tag:
NO-GO until exact-main-SHA CI and explicit tag authorization; tag push also
needs image-publication approval. Known hardware/design limits are explicitly
accepted/documented release scope, not silently completed tasks.
