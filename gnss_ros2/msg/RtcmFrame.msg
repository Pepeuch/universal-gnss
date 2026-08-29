# Local NTRIP receipt time expressed in the ROS clock domain. Consumers must
# capture their own monotonic receipt time for freshness/liveness calculations.
builtin_interfaces/Time stamp
uint16 message_type
uint8[] data
