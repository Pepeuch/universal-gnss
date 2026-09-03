# Universal GNSS — ROS2 Policy v1.4

Load when modifying `.cpp`, `.cc`, `.hpp`, or `.hh` under `gnss_ros2/`, or when ROS2-specific
validation applies.

Universal GNSS ROS2 integration must preserve canonical Universal GNSS semantics rather
than redefining them for a downstream robot.

Also run the affected ROS2/package/integration validation appropriate to the changed
contract.

When ROS2 surfaces, messages, diagnostics, launch behaviour, runtime status, correction
observability, or operator visibility change, evaluate downstream implications under
root `AGENTS.md` and `MOWGLINEXT_TODO.md`.

Do not translate canonical GNSS/geodetic concepts into robot-specific public API terms
inside Universal GNSS merely for downstream convenience.
