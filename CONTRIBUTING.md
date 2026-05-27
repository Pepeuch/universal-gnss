# Contributing

## Design rules

- Keep gnss_core independent from ROS 2.
- Do not add vendor-specific hacks to the public runtime model.
- Map vendor data into capability-driven common structures.
- Keep parsing, transport, receiver configuration, and ROS 2 publishing separated.
- Add tests for every parser and runtime mapper.
- Vendor support

## New vendor support should include:

- protocol documentation reference
- parser implementation
- runtime mapping
- capability list
- sample logs or test vectors
- tests
- Commit style

## Use conventional commits:

- feat:
- fix:
- docs:
- test:
- refactor:
- chore: