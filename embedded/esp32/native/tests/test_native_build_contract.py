# SPDX-License-Identifier: LGPL-3.0-only
"""Regression for the actual native compile-command language gate."""
import importlib.util
import copy
import gzip
import json
import stat
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
    def test_s3_uart_flash_safety_is_required_without_changing_c3_policy(self):
        config = ['CONFIG_UART_ISR_IN_IRAM=y']
        self.assertEqual(module.sdk_gnss_uart_memory(config, True),
                         {'required': True, 'isr_iram': True, 'driver_buffers': 'internal'})
        for bad in ([], ['# CONFIG_UART_ISR_IN_IRAM is not set'],
                    config + ['CONFIG_RINGBUF_PLACE_ISR_FUNCTIONS_INTO_FLASH=y']):
            with self.assertRaises(ValueError):
                module.sdk_gnss_uart_memory(bad, True)
        self.assertEqual(module.sdk_gnss_uart_memory([], False),
                         {'required': False, 'isr_iram': False, 'driver_buffers': 'SDK_default'})

    def test_network_symbols_require_the_exact_execution_path(self):
        shared=['universal_gnss_ntrip::NtripClient::Read(',
                'universal_gnss_transport::TcpClientTransport::Open(']
        cooperative=['ug::embedded::NtripPublisher::BeginFrame(',
                     'ug::embedded::NtripPublisher::ProgressFrame(',
                     'ug::embedded::WithdrawCorrectionDestination(',
                     'ug::embedded::CorrectionDestinationRetired(', 'CooperativeHelper(']
        cooperative += ['ug::embedded::InitializeNetworkAllocation(']
        legacy=['ug::embedded::NtripPublisher::Send(']
        module.check_network_engine('\n'.join(shared+cooperative),'gateway')
        module.check_network_engine('\n'.join(shared+legacy),'relay-ntrip')
        for index in range(len(shared+cooperative)):
            values=shared+cooperative
            with self.assertRaises(ValueError):
                module.check_network_engine('\n'.join(values[:index]+values[index+1:]),'gateway')
        with self.assertRaises(ValueError):
            module.check_network_engine('\n'.join(shared+legacy),'gateway')
        with self.assertRaises(ValueError):
            module.check_network_engine('\n'.join(shared+cooperative),'relay-ntrip')

    def test_immutable_firmware_identity_and_separate_transports(self):
        for transport in ('http','https'):
            identity=builder.firmware_identity('gateway','waveshare-s3-eth',transport,'a'*40,'b'*40,'v0.7.5-RC1')
            self.assertEqual(identity['source_sha'],'a'*40)
            self.assertEqual(identity['source_tree'],'b'*40)
            self.assertEqual(identity['web_transport'],transport)
            self.assertFalse(identity['modified'])
        self.assertTrue(builder.firmware_identity('gateway','waveshare-s3-eth','https','a'*40,'b'*40,'v0.7.5-RC1',True)['modified'])
        for source,tree,version in (('short','b'*40,'v0.7.5-RC1'),('a'*40,'short','v0.7.5-RC1'),('a'*40,'b'*40,'development')):
            with self.assertRaises(ValueError):builder.firmware_identity('gateway','waveshare-s3-eth','https',source,tree,version)

    def test_fresh_https_bootstrap_precedes_radio_and_preserves_http(self):
        root=Path(__file__).resolve().parents[1]
        gateway=(root/'main/gateway.cpp').read_text()
        self.assertLess(gateway.index('PrepareWebIdentityAtBoot();'),gateway.index('const BootStep steps[]'))
        self.assertLess(gateway.index('PrepareWebIdentityAtBoot();'),gateway.index('StartLocalCommissioning(RestartNetworkServices)'))
        self.assertIn('CONFIG_IDF_TARGET_ESP32S3 && !CONFIG_UG_WEB_HTTP',gateway)
        web=(root/'../components/ug_web/src/ug_web.cpp').read_text()
        self.assertIn('return FinishHttpsStartup(bootstrap.reason, bootstrap.sdk_error)',web)
        tls=(root/'../components/ug_platform/src/ug_tls_bootstrap.cpp').read_text()
        self.assertLess(tls.index('bootloader_random_enable();'),tls.index('result = BootstrapTls(backend);'))
        self.assertGreater(tls.index('bootloader_random_disable();'),tls.index('result = BootstrapTls(backend);'))
        self.assertIn('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT',tls)
        self.assertNotIn('nvs_erase',tls)
        self.assertNotIn('nvs_flash_erase',gateway)
        self.assertNotIn('"development"',(root/'CMakeLists.txt').read_text())

    def test_web_transport_is_explicit_and_does_not_downgrade_tls(self):
        s3=['CONFIG_IDF_TARGET_ESP32S3=y']
        self.assertEqual(module.sdk_web_transport(s3),'https')
        self.assertEqual(module.sdk_web_transport(s3+['CONFIG_UG_WEB_HTTP=y'],'http'),'http')
        for lines,expected in ((s3,'http'),(s3+['CONFIG_UG_WEB_HTTP=y'],'https'),
                               (['CONFIG_UG_WEB_HTTP=y'],'http'),(s3,'automatic')):
            with self.assertRaises(ValueError):module.sdk_web_transport(lines,expected)
        for profile,board,mode in (('rover','supermini-c3','http'),
                                   ('relay-ntrip','supermini-c3','http'),
                                   ('gateway','waveshare-s3-eth','automatic')):
            with self.assertRaises(ValueError):
                builder.build('/tmp/not-created-http-invalid',profile,board,False,web_transport=mode)

    def test_s3_owner_oom_reserve_is_explicit_and_not_a_c3_requirement(self):
        config=['CONFIG_COMPILER_CXX_EXCEPTIONS=y',
                'CONFIG_COMPILER_CXX_EXCEPTIONS_EMG_POOL_SIZE=2048']
        self.assertEqual(module.sdk_owner_oom_recovery(config,True),
                         {'enabled':True,'emergency_bytes':2048})
        for bad in ([],config[:1],config[1:],[config[0],config[1].replace('2048','0')]):
            with self.assertRaises(ValueError):module.sdk_owner_oom_recovery(bad,True)
        self.assertEqual(module.sdk_owner_oom_recovery([],False),
                         {'enabled':False,'emergency_bytes':0})

    def test_network_boot_and_s3_tls_reject_insufficient_effective_memory_settings(self):
        s3 = ['CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768', 'CONFIG_SPIRAM_USE_MALLOC=y',
              'CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y', 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024']
        self.assertEqual(module.sdk_boot_memory(s3, True),
                         {'main_stack_bytes':32768, 'tls_external':True})
        for i in range(len(s3)):
            with self.assertRaises(ValueError):module.sdk_boot_memory(s3[:i]+s3[i+1:], True)
        for replacement in ('CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192', 'CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384'):
            with self.assertRaises(ValueError):module.sdk_boot_memory([replacement]+s3[1:], True)
        self.assertEqual(module.sdk_boot_memory(s3[:1], False)['tls_external'], False)

    def test_readonly_source_keeps_its_modes_while_network_copy_gets_its_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root/'source'
            source.mkdir()
            for name, content in (('idf_component.yml','minimal'),
                                  ('idf_component.network.yml','pinned-network'),
                                  ('gateway.cpp','unchanged-source')):
                (source/name).write_text(content)
                (source/name).chmod(0o444)
            source.chmod(0o555)
            try:
                destination = root/'network'
                builder.copy_main(source, destination, True)
                self.assertEqual((destination/'idf_component.yml').read_text(), 'pinned-network')
                self.assertEqual((destination/'gateway.cpp').read_text(), 'unchanged-source')
                (destination/'idf_component.yml').write_text('generated-only-change')
                self.assertEqual((source/'idf_component.yml').read_text(), 'minimal')
                self.assertEqual(stat.S_IMODE(source.stat().st_mode), 0o555)
                self.assertTrue(all(stat.S_IMODE(p.stat().st_mode) == 0o444 for p in source.iterdir()))
                builder.copy_main(source, root/'rover', False)
                self.assertEqual((root/'rover/idf_component.yml').read_text(), 'minimal')
            finally:
                # Only this test's temporary copies; never the repository.
                for directory in (source, root/'network', root/'rover'):
                    if directory.exists(): directory.chmod(0o755)

    def test_pair_and_channel_are_runtime_settings_not_build_parameters(self):
        for peer, channel in [('02:00:00:00:00:02',1), (None,1), (None,6)]:
            with self.assertRaisesRegex(ValueError, 'runtime settings'):
                builder.build('/tmp/not-created-ug-radio-test', 'rover', 'supermini-c3', False,
                              peer_mac=peer, channel=channel)

    def test_dependency_locks_reject_inventory_version_hash_and_source_drift(self):
        pins = json.loads((Path(__file__).resolve().parents[2]/'migration/dependencies.json').read_text())['managed']
        good = {'target':'esp32s3','dependencies':copy.deepcopy(pins)}
        builder.validate_dependency_lock(good, 'esp32s3')
        for name in pins:
            for field, bad in (('version','999.0'), ('component_hash','0'*64)):
                changed = copy.deepcopy(good)
                changed['dependencies'][name][field] = bad
                with self.assertRaises(ValueError):
                    builder.validate_dependency_lock(changed, 'esp32s3')
            changed = copy.deepcopy(good)
            changed['dependencies'][name]['source']['type'] = 'git'
            with self.assertRaises(ValueError):
                builder.validate_dependency_lock(changed, 'esp32s3')
        for changed in ({**good,'target':'esp32c3'},
                        {**good,'dependencies':{}},
                        {**good,'dependencies':{**good['dependencies'],'unexpected':{}}}):
            with self.assertRaises(ValueError):
                builder.validate_dependency_lock(changed, 'esp32s3')

    def test_wire_logging_keeps_warnings_and_runtime_channel_messages(self):
        def entry(name, flags):
            return {'file':f'/ug/embedded/esp32/components/{name}/src/{name}.c',
                    'command':'cc -c ' + flags}
        good = [entry('lora_transport', '-DLOG_LOCAL_LEVEL=ESP_LOG_WARN'),
                entry('rtcm_filter', '-DLOG_LOCAL_LEVEL=ESP_LOG_WARN'),
                entry('espnow_rtcm', '')]
        module.check_wire_logging(good, 'gateway', 3)
        for profile, maximum in (('rover', 0), ('gateway', 0), ('relay-ntrip', 2)):
            quiet = [entry('lora_transport', ''), entry('rtcm_filter', ''), entry('espnow_rtcm', '')]
            module.check_wire_logging(quiet, profile, maximum)
            with self.assertRaises(ValueError):
                module.check_wire_logging(good, profile, maximum)
        for flags in ('', '-DLOG_LOCAL_LEVEL=ESP_LOG_NONE', '-DLOG_LOCAL_LEVEL=ESP_LOG_INFO',
                      '-DLOG_LOCAL_LEVEL=ESP_LOG_WARN -ULOG_LOCAL_LEVEL',
                      '-DLOG_LOCAL_LEVEL=ESP_LOG_WARN -DLOG_LOCAL_LEVEL=ESP_LOG_INFO'):
            with self.assertRaises(ValueError):
                module.check_wire_logging([entry('lora_transport', flags), *good[1:]], 'gateway', 3)
        with self.assertRaises(ValueError):
            module.check_wire_logging(good[:2] + [entry('espnow_rtcm', '-DLOG_LOCAL_LEVEL=ESP_LOG_WARN')], 'gateway', 3)
        with self.assertRaises(ValueError):
            module.check_wire_logging(good[:1], 'gateway', 3)

    def test_wire_selftest_is_linked_only_in_admin_profiles(self):
        unit = '/ug/embedded/esp32/components/ug_selftest/src/ug_wire_selftest.cpp'
        symbol = '001 T ug::embedded::RunWireSelfTest()'
        module.check_wire_selftest_boundary([], '', 'rover')
        for profile in ('gateway', 'relay-ntrip'):
            module.check_wire_selftest_boundary([unit], symbol, profile)
            for inputs, symbols in (([], ''), ([unit], ''), ([], symbol), ([unit, unit], symbol)):
                with self.assertRaises(ValueError):
                    module.check_wire_selftest_boundary(inputs, symbols, profile)
        for inputs, symbols in (([unit], symbol), ([], symbol), ([unit], '')):
            with self.assertRaises(ValueError):
                module.check_wire_selftest_boundary(inputs, symbols, 'rover')

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
            (source/'pairing.js').write_text('public pairing helper')
            packed = root / 'esp-idf/ug_web/web_assets'
            packed.mkdir(parents=True)
            application = b'fake-app-prefix'
            for name in ('index.html', 'app.js', 'style.css'):
                raw = ('content-' + name).encode()
                (source/name).write_bytes(raw)
                if name=='app.js':raw=(Path(__file__).resolve().parents[4]/'docs/flasher/portable.js').read_bytes()+b'\n'+(source/'pairing.js').read_bytes()+b'\n'+raw
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
