# Universal GNSS — Hardware Validation Policy v1.3

Load when correctness depends on behaviour that source code, deterministic tests, and
documentation cannot prove without physical hardware.

This policy prevents speculative software archaeology from substituting for physical
evidence.

## 1. Hardware boundary

A hardware boundary exists when correctness materially depends on any of:

- physical receiver/device behaviour;
- electrical behaviour;
- firmware-specific runtime behaviour;
- serial/USB transport;
- kernel/driver behaviour;
- reconnect/hotplug/re-enumeration;
- RF/correction reception;
- timing at a physical link;
- device/bridge queues;
- power-cycle/incarnation semantics.

When that boundary is reached, stop the dependent software investigation unless new
physical evidence is available.

Do not attempt to “prove” a physical guarantee through increasingly speculative source,
kernel, driver, or historical archaeology when the missing fact is inherently physical.

Continue only independent software work whose correctness does not depend on the missing
physical result.

## 2. Validation labels

Hardware validation is orthogonal to finding STATUS and SCOPE.

Use:

- `HARDWARE_REQUIRED` when physical evidence is part of acceptance and is currently unavailable;
- `HARDWARE_PENDING` when implementation can legitimately be considered complete without
  that physical result, but physical validation remains desirable/required for deployment confidence.

Do not encode missing hardware proof as evidence staleness.

If physical validation is part of acceptance, do not force `IMPLEMENTED`; `BLOCKED` or
`PARTIAL` may be the correct status.

## 3. Required physical baseline

Record the exact validated baseline:

```text
Hardware:
Firmware:
Host/OS/kernel:
Driver/interface:
Physical topology:
Test procedure:
Result:
Acceptance criterion:
What would invalidate this result:
```

Physical evidence applies only to that recorded baseline and justified equivalents.
Do not generalize across receiver models, firmware, bridges, kernels, drivers, USB
topologies, or power arrangements without evidence.

## 4. Unavailable hardware

If hardware is unavailable:

1. record the exact missing physical proof;
2. record the required test matrix/procedure;
3. record the acceptance criterion;
4. classify the finding correctly (`HARDWARE_REQUIRED` where applicable);
5. checkpoint the exact next physical action;
6. if the hardware dependency must survive the local workspace/session, promote the compact state to `.agent/shared/checkpoints/blocked/` with the exact hardware matrix and unblock condition;
7. stop the dependent branch.

Do not continue spending model/context budget on a branch that cannot be resolved without
new physical evidence.

## 5. Recovery/incarnation claims

Treat strong claims such as “old bytes cannot reach the new transaction”, “hotplug
creates a clean receiver incarnation”, or “close/reopen flushes every device/bridge
queue” as hardware/transport claims unless a documented deterministic guarantee proves
them end-to-end.

Userspace parser reset, local generation counters, `tcflush`, close/reopen, process
restart, path renumbering, or observed USB disappearance are not by themselves proof of
a receiver-side prior-byte cutoff.

Only a validated recovery mechanism may establish a new trusted transport-incarnation
boundary for such semantics.


## 6. Hardware checkpoint retention

A hardware-gated finding is a strong candidate for a versioned `BLOCKED` checkpoint because the missing physical matrix may be executed much later or by another contributor. Preserve only the exact hardware/firmware/interface/topology matrix required, deterministic software evidence already completed, acceptance criterion, blocker/unblock condition, next physical procedure, and invalidation conditions. Do not preserve speculative protocol archaeology once the physical boundary is established.
