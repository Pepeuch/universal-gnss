// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
// Comparator selection: qualify task-only network children before evaluating
// the external I/O stack. The DNS/TLS/NVS helper always retains an internal stack.
#ifndef UG_NETWORK_STACK_PSRAM
#define UG_NETWORK_STACK_PSRAM 0
#endif
