# Shared checkpoint policy

This directory is intentionally versioned agent memory.

```text
.agent/shared/checkpoints/
├── INDEX.md
├── active/
├── blocked/
├── retained/
└── closed/
```

The canonical backlog/status source remains the repository TODO/manifest/ledger. Working/session checkpoints remain under ignored `.agent/checkpoints/` and are never staged. Before promoting a checkpoint, remove logs, transcripts, copied diffs, secrets, stale session noise, and chronology that does not help future continuation.
