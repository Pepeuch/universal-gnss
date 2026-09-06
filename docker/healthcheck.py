#!/usr/bin/env python3
"""Bounded ROS responsiveness probe for enabled container components."""

from __future__ import annotations

import argparse

import rclpy
from std_srvs.srv import Trigger


DEFAULT_SERVICES = (
    "/universal_gnss_receiver/get_health",
    "/universal_gnss_ntrip/get_health",
)


def service_is_responsive(node, service_name: str, timeout_s: float) -> bool:
    client = node.create_client(Trigger, service_name)
    if not client.wait_for_service(timeout_sec=timeout_s):
        return False
    future = client.call_async(Trigger.Request())
    rclpy.spin_until_future_complete(node, future, timeout_sec=timeout_s)
    if not future.done() or future.exception() is not None:
        return False
    response = future.result()
    return response is not None and response.success


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args(argv)
    if args.timeout <= 0.0:
        parser.error("--timeout must be positive")

    rclpy.init(args=None)
    node = rclpy.create_node("universal_gnss_container_healthcheck")
    try:
        healthy = all(
            service_is_responsive(node, service_name, args.timeout)
            for service_name in DEFAULT_SERVICES
        )
        return 0 if healthy else 1
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
