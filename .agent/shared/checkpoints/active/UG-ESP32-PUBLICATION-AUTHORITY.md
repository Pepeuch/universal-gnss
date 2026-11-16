# Historical-source automatic tag authority

Lifecycle: ACTIVE
Status: OPEN (general future recovery policy not implemented)
Scope: DEPLOYMENT
Canonical tracking: [TODO](../../../../TODO.md), publication-authority follow-up.
Publication-code baseline: `d3c9e0688bc6756bc0f493397d03c7bd3c2f1b40`,
ancestor of `e5cb849dd2e1830d98dfb5ec893af2648a91bd40`.

## Proven incident / current boundary

The missing-list/glob fixes and the approved source's actual publication succeeded;
see [retained recovery evidence](../retained/UG-ESP32-PUBLICATION-RECOVERY.md).
A prior automatic `POST git/refs` failed 403 for an old workflow-changing commit
without a preexisting ref, despite contents:write. The one-off authorized normal
tag push enabled that source's publication. It does NOT qualify future automatic
tag creation for every historical workflow-changing SHA.
An already-published source remains visible regardless of alpha/beta/stable channel;
visibility, automatic compilation and authorization for a new release are distinct.

## Exact next step

When publication work resumes, review explicit maintainer tag preparation versus a
suitably scoped, approved bot/app authority, then test on the exact allowed source.
Do not request broader permissions or create a ref during checkpoint maintenance.
Keep approved source/tree checks, successful main CI requirements, immutable prior
assets, shared Pages lock and fail-closed catalogue recovery. No automatic approval
of new source revisions or publication of this working branch is authorized.
Reinspect remote policy only when performing that separately authorized work;
current incident records are not a live protection/permission audit.
