#!/usr/bin/env python3
"""Focused tests for Docker component-responsiveness health semantics."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[2] / "docker" / "healthcheck.py"
SPEC = importlib.util.spec_from_file_location("universal_gnss_healthcheck", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeFuture:
    def __init__(self, *, done: bool = True, success: bool = True):
        self._done = done
        self._response = mock.Mock(success=success)

    def done(self):
        return self._done

    def exception(self):
        return None

    def result(self):
        return self._response


class FakeClient:
    def __init__(self, *, available: bool = True, done: bool = True, success: bool = True):
        self.available = available
        self.future = FakeFuture(done=done, success=success)

    def wait_for_service(self, timeout_sec):
        return self.available

    def call_async(self, request):
        return self.future


class FakeNode:
    def __init__(self, client):
        self.client = client

    def create_client(self, service_type, service_name):
        return self.client


class DockerHealthcheckTests(unittest.TestCase):
    @mock.patch.object(MODULE.rclpy, "spin_until_future_complete")
    def test_responsive_component_is_healthy_independent_of_gnss_quality(self, spin):
        node = FakeNode(FakeClient(success=True))

        self.assertTrue(MODULE.service_is_responsive(node, "/receiver/get_health", 0.1))
        spin.assert_called_once()

    def test_missing_enabled_component_is_unhealthy(self):
        node = FakeNode(FakeClient(available=False))

        self.assertFalse(MODULE.service_is_responsive(node, "/receiver/get_health", 0.1))

    @mock.patch.object(MODULE.rclpy, "spin_until_future_complete")
    def test_unresponsive_enabled_component_is_unhealthy(self, spin):
        node = FakeNode(FakeClient(done=False))

        self.assertFalse(MODULE.service_is_responsive(node, "/receiver/get_health", 0.1))


if __name__ == "__main__":
    unittest.main()
