# SPDX-License-Identifier: LGPL-3.0-only
"""Regression for the actual native compile-command language gate."""
import importlib.util
import gzip
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_verify', Path(__file__).resolve().parents[1]/'verify.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
build_spec = importlib.util.spec_from_file_location('native_builder', Path(__file__).resolve().parents[1]/'build.py')
builder = importlib.util.module_from_spec(build_spec)
build_spec.loader.exec_module(builder)


class NativeBuildContract(unittest.TestCase):
    def test_network_defaults_preserve_small_relay_and_exclude_rover(self):
        for role in ('gateway', 'relay-ntrip'):
            self.assertEqual(builder.network_sdk_defaults(role, 'waveshare-s3-eth'), 'CONFIG_LWIP_MAX_SOCKETS=20\n')
            self.assertEqual(builder.network_sdk_defaults(role, 'supermini-c3'), 'CONFIG_LWIP_MAX_SOCKETS=10\n')
        self.assertEqual(builder.network_sdk_defaults('rover', 'supermini-c3'), '')
        with self.assertRaises(ValueError):
            builder.network_sdk_defaults('unknown', 'supermini-c3')
        config = ['CONFIG_LWIP_MAX_SOCKETS=20', 'CONFIG_LWIP_MAX_ACTIVE_TCP=16',
                  'CONFIG_LWIP_MAX_LISTENING_TCP=16', 'CONFIG_LWIP_MAX_UDP_PCBS=16']
        self.assertEqual(module.sdk_network_capacity(config),
                         {'sockets':20,'tcp_active':16,'tcp_listeners':16,'udp_pcbs':16,'mdns_socket_mode':False})
        self.assertTrue(module.sdk_network_capacity(config + ['CONFIG_MDNS_NETWORKING_SOCKET=y'])['mdns_socket_mode'])
        for missing in range(len(config)):
            with self.assertRaises(ValueError):
                module.sdk_network_capacity(config[:missing] + config[missing+1:])

    def test_rover_uart_requires_explicit_reviewed_pins_role_and_baud(self):
        builder.validate_rover_output('rover', 'supermini-c3', None, 115200)
        for pin in (4, 5, 6, 7, 10):
            builder.validate_rover_output('rover', 'supermini-c3', pin, 921600)
        for role, board, pin, baud in (
                ('gateway', 'supermini-c3', 4, 115200),
                ('rover', 'waveshare-s3-eth', 4, 115200),
                ('rover', 'supermini-c3', None, 921600),
                ('rover', 'supermini-c3', 4, 9600)):
            with self.assertRaises(ValueError):
                builder.validate_rover_output(role, board, pin, baud)
        for pin in (-1, 0, 2, 3, 8, 9, 11, 18, 19, 20, 100):
            with self.assertRaises(ValueError):
                builder.validate_rover_output('rover', 'supermini-c3', pin, 115200)

    def test_linked_web_assets_cannot_be_stale_missing_or_duplicated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'source'
            source.mkdir()
            packed = root / 'esp-idf/ug_web/web_assets'
            packed.mkdir(parents=True)
            application = b'fake-app-prefix'
            for name in ('index.html', 'app.js', 'style.css'):
                raw = ('content-' + name).encode()
                (source/name).write_bytes(raw)
                compressed = gzip.compress(raw, mtime=0)
                (packed/(name+'.gz')).write_bytes(compressed)
                application += compressed
            self.assertEqual(len(module.check_web_assets(root, source, application)), 3)
            for invalid in (b'no-assets', application * 2):
                with self.assertRaises(ValueError):
                    module.check_web_assets(root, source, invalid)
            (source/'index.html').write_text('newer-source')
            with self.assertRaises(ValueError):
                module.check_web_assets(root, source, application)

    def test_project_standards_are_effective_not_minimum_or_overridden(self):
        commands = [
            {'file': '/ug/core/lib.cpp', 'command': 'g++ -std=gnu++26 -std=gnu++17 -c /ug/core/lib.cpp'},
            {'file': '/ug/embedded/esp32/components/radio/radio.c', 'command': 'gcc -std=gnu23 -std=gnu17 -c radio.c'},
            {'file': '/build/project/main/gateway.cpp', 'command': 'g++ -std=c++17 -c gateway.cpp'},
            {'file': '/sdk/file.cpp', 'command': 'g++ -std=gnu++26 -c file.cpp'}]
        checked = module.project_standards(commands, '/ug', '/build/project/main')
        self.assertEqual(checked, {'c': ['gnu17'], 'cxx': ['c++17', 'gnu++17']})
        for invalid in ['g++ -std=gnu++17 -std=gnu++26 -c lib.cpp', 'g++ -c lib.cpp']:
            with self.assertRaises(ValueError):
                module.project_standards([{'file': '/ug/core/lib.cpp', 'command': invalid}], '/ug', '/build/project/main')
        with self.assertRaises(ValueError):
            module.project_standards([{'file': '/ug/embedded/esp32/components/radio/radio.c',
                                      'command': 'gcc -std=gnu23 -c radio.c'}], '/ug', '/build/project/main')


if __name__ == '__main__':
    unittest.main()
