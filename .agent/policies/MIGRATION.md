# Universal GNSS — Migration Policy v1.4

Load when work uses historical PRs, forks, branches, patches, or legacy implementations
as guidance.

Historical material is behavioural evidence, not automatically mergeable code.

Before reusing historical work:

1. state the behavioural question being answered;
2. identify the old contract/defect addressed;
3. inspect the current architecture and ownership boundary;
4. compare lifecycle, interfaces, configuration, and tests;
5. separate verified behaviour from assumptions;
6. cite exact files/symbols/commits/tests where consequential.

Classify each historical change using exactly one of:

- `PORT`
- `ADAPT`
- `ALREADY PRESENT`
- `SUPERSEDED`
- `REJECT`

Do not invent synonyms.

- `PORT` → behaviour and mechanism remain valid with direct transfer appropriate;
- `ADAPT` → required behaviour remains valid but current architecture needs a different implementation;
- `ALREADY PRESENT` → current repository already satisfies the required behaviour;
- `SUPERSEDED` → current architecture/contract replaced the historical need;
- `REJECT` → historical behaviour/mechanism is not valid for the current contract.

Do not cherry-pick, restore, or mechanically reproduce historical implementation merely
because it once worked.

When MIGRATION and LONGTASK are both active, use this vocabulary inside checkpoint
semantic decisions.


## Durable migration evidence

When a migration finding is completed but its historical comparison is likely to guide later ports/adaptations, classify its shared checkpoint as `RETAINED` rather than keeping a full working checkpoint indefinitely. Retain only the behavioural contract, exact historical/current references, PORT/ADAPT/ALREADY PRESENT/SUPERSEDED/REJECT decisions, invalidation conditions, and remaining dependent work. Promote mature project-level conclusions to normal versioned migration/architecture documentation when appropriate.
