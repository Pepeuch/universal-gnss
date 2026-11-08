# Version policy after v0.7.4

Current integration target: **main**. Any push of this prepared work, if later
approved, targets main directly. This document does not authorize a push or
create a development/maintenance branch.

| Series or first milestone | Purpose / entry criterion |
| --- | --- |
| `v0.7.x` | Stable maintenance: scoped fixes, compatibility and security work. |
| `v0.8.0-alpha.1` | Optional experimental development when early publication is useful. |
| `v0.8.0-beta.1` | First beta when new features are sufficiently advanced for broader testing. |
| `v0.8.0-rc.1` | Feature freeze; final regression, deployment and physical qualification. |
| `v0.8.0` | Next stable after explicit maintainer acceptance of its release evidence. |

Increment the numeric prerelease suffix for later candidates. Prerelease tags
are immutable identities, not promises of production readiness. Keep stable
and prerelease image references explicit; deployments should pin verified
immutable digests rather than silently follow an experimental tag.

## Independent development and maintenance

After v0.7.4 integration, use isolated worktrees and separate lines when v0.8
work begins. A future `release/0.7` maintenance line and `next/0.8` development
line can both start from the approved v0.7.4 main commit. Their creation and
remote publication are a later repository decision; neither is performed here.
The existing remote dev is behind main and must not be mistaken for the latest
runtime baseline or reset without authorization.

Land scoped stable fixes on the maintenance line and forward-port them to v0.8
with their regression tests. Experimental v0.8 changes must not enter a v0.7.x
release through a bulk merge. Main remains the present approved integration
target; changing its long-term role requires an explicit documented decision.
Use separate TODO milestones/evidence baselines while retaining the single
canonical backlog classification source.

The current publication workflow matches **every `v*` tag**, including alpha,
beta and RC tags. Pushing any such tag publishes images automatically. A future
branching policy must therefore retain explicit tag/image publication approval
and exact-SHA CI gates for both stable and experimental releases.
