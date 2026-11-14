"""Native firmware CI contracts; no SDK or hardware required by these tests."""
import copy
from contextlib import nullcontext
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('native_ci', ROOT/'embedded/esp32/native/ci.py')
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class NativeCI(unittest.TestCase):
    def test_project_commands_follow_disconnect_and_host_failure_stops_firmware_build(self):
        identity = {'source_object':'f'*40}
        inventory = {'host_test_only':{'quickjs':{'version':'1.19.4'}}}
        info = {'Config':{'User':f'{os.getuid()}:{os.getgid()}'},'Mounts':[],
                'HostConfig':{'Devices':[], 'Privileged':False},'NetworkSettings':{'Networks':{}}}
        for fail_host in (False, True):
            events=[]
            def execute(command, **kwargs):
                if command[:3] == ['docker','network','disconnect']: events.append('disconnect')
                output=json.dumps([info]).encode() if command[:2] == ['docker','inspect'] else b'archive'
                return subprocess.CompletedProcess(command,0,output)
            def sdk_execute(cid, script, **kwargs):
                if 'cmake -S' in script:
                    events.append('host-tests')
                    if fail_host: raise subprocess.CalledProcessError(9, ['host-tests'])
                elif 'idf.py -C' in script:
                    events.append('firmware-build')
                elif '/build.py --output /tmp/ug-ci-b' in script:
                    events.append('reconstruction')
                output=b'{"sha256":"same-app"}' if '/verify.py' in script else b'libssl-dev 3.0'
                return subprocess.CompletedProcess([],0,output)
            with patch.object(ci,'sdk_container',return_value=nullcontext('owned')), \
                 patch.object(ci,'run',side_effect=execute), patch.object(ci,'sdk',side_effect=sdk_execute):
                if fail_host:
                    with self.assertRaises(subprocess.CalledProcessError):
                        ci.validate_container(ROOT,identity,inventory,'base','deps','s3-gateway')
                    self.assertEqual(events, ['disconnect','host-tests'])
                else:
                    report=ci.validate_container(ROOT,identity,inventory,'base','deps','s3-gateway')
                    self.assertEqual(report['host_tests'],'pass')
                    self.assertEqual(events, ['disconnect','host-tests','firmware-build','reconstruction'])

    def test_host_dependency_image_has_empty_context_and_no_project_build(self):
        recipe = (ROOT/'embedded/esp32/native/Dockerfile.ci').read_bytes()
        text = recipe.decode()
        self.assertIn('FROM ${SDK_IMAGE}', text)
        self.assertIn('libssl-dev', text)
        self.assertNotRegex(text, r'(?m)^\s*(COPY|ADD|VOLUME)\b')
        self.assertNotIn('idf.py', text)
        self.assertTrue(text.rstrip().endswith('USER 1000:1000'))
        with patch.object(ci,'run') as execute, \
             patch.object(ci.subprocess,'run',return_value=subprocess.CompletedProcess([],0)) as cleanup:
            with ci.host_dependency_image('official-sdk', recipe) as tag:
                self.assertTrue(tag.startswith('ug-native-host-deps:'))
            execute.assert_called_once_with(['docker','build','--build-arg','SDK_IMAGE=official-sdk','--tag',tag,'-'], data=recipe)
            cleanup.assert_called_once_with(['docker','image','rm',tag],timeout=30,check=False)

    def test_cleanup_failure_cannot_turn_a_success_green_or_hide_original_error(self):
        for manager, args in ((ci.sdk_container, ('sdk',)),
                              (ci.host_dependency_image, ('sdk', b'FROM sdk'))):
            with patch.object(ci.os,'getuid',return_value=1001), \
                 patch.object(ci.os,'getgid',return_value=1001), \
                 patch.object(ci,'run',return_value=subprocess.CompletedProcess([],0,b'owned-id\n')), \
                 patch.object(ci.subprocess,'run',return_value=subprocess.CompletedProcess([],1)):
                with self.assertRaisesRegex(RuntimeError,'cleanup failed'):
                    with manager(*args): pass
                with self.assertRaisesRegex(ValueError,'original error'):
                    with manager(*args): raise ValueError('original error')

    def test_immutable_official_sdk_digest_is_required(self):
        inventory = json.loads((ROOT/'embedded/esp32/migration/dependencies.json').read_text())
        self.assertRegex(ci.sdk_image(inventory), r'^espressif/idf@sha256:[0-9a-f]{64}$')
        for image in ('espressif/idf:v6.1', 'local-sdk:latest', 'other/idf@sha256:'+'a'*64):
            bad = copy.deepcopy(inventory)
            bad['sdk']['image'] = image
            with self.assertRaises(ValueError): ci.sdk_image(bad)

    def test_container_boundary_rejects_network_mount_device_root_and_privilege(self):
        good = {'Config':{'User':'1001:1001'},'Mounts':[],
                'HostConfig':{'Devices':[], 'Privileged':False},'NetworkSettings':{'Networks':{}}}
        ci.require_isolation(good, 1001, 1001)
        for section, field, value in (('Config','User','0:0'),('HostConfig','Devices',[{}]),
                ('HostConfig','Privileged',True),('NetworkSettings','Networks',{'bridge':{}})):
            bad = copy.deepcopy(good);bad[section][field]=value
            with self.assertRaises(ValueError): ci.require_isolation(bad, 1001, 1001)
        bad = copy.deepcopy(good);bad['Mounts']=[{'Destination':'/var/run/docker.sock'}]
        with self.assertRaises(ValueError): ci.require_isolation(bad, 1001, 1001)

    def test_workflow_has_exact_supported_matrix_and_no_publication(self):
        workflow = yaml.load((ROOT/'.github/workflows/esp32.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(workflow['permissions'], {'contents':'read'})
        self.assertEqual(set(workflow['jobs']), {'firmware','gate'})
        job = workflow['jobs']['firmware']
        self.assertNotIn('if', job)
        self.assertEqual(job['strategy'], {'fail-fast':'false','matrix':{'variant':list(ci.VARIANTS)}})
        self.assertEqual(job['steps'][0]['with']['persist-credentials'], 'false')
        self.assertEqual(len(job['steps']), 2)
        self.assertIn('embedded/esp32/native/ci.py', job['steps'][1]['run'])
        self.assertIn('test "$(id -u)" != 0', job['steps'][1]['run'])
        self.assertNotIn('pull_request_target', workflow['on'])
        self.assertNotIn('secrets.', json.dumps(workflow))
        self.assertNotIn('upload-artifact', json.dumps(workflow))

    def test_gate_is_always_and_fail_closed_for_every_matrix_result(self):
        workflow = yaml.load((ROOT/'.github/workflows/esp32.yml').read_text(), Loader=yaml.BaseLoader)
        gate = workflow['jobs']['gate']
        self.assertEqual(gate['if'], 'always()')
        self.assertEqual(gate['needs'], ['firmware'])
        script = gate['steps'][0]['run']
        for state in ('success','failure','cancelled','skipped',''):
            result = subprocess.run(['bash','-c',script], env={**os.environ,'FIRMWARE_RESULT':state})
            self.assertEqual(result.returncode == 0, state == 'success')

    def test_variants_preserve_minimal_rover_and_explicit_uart(self):
        self.assertEqual(set(ci.VARIANTS), {'s3-gateway','c3-relay','c3-rover-usb','c3-rover-uart'})
        self.assertIn('--local-gnss', ci.build_arguments('s3-gateway'))
        for name in ('c3-relay','c3-rover-usb','c3-rover-uart'):
            self.assertNotIn('--local-gnss', ci.build_arguments(name))
        self.assertNotIn('--rover-uart-tx', ci.build_arguments('c3-rover-usb'))
        self.assertIn('--rover-uart-tx 4', ci.build_arguments('c3-rover-uart'))

    def test_source_identity_distinguishes_commit_and_staged_tree(self):
        commit = ci.source_identity(ROOT,'HEAD')
        tree = ci.source_identity(ROOT,commit['source_tree'])
        self.assertEqual(commit['source_type'], 'commit')
        self.assertEqual(tree['source_type'], 'tree')
        self.assertIsNone(tree['source_commit'])
        self.assertEqual(commit['source_tree'], tree['source_tree'])
        blob = subprocess.check_output(['git','rev-parse','HEAD:README.md'], cwd=ROOT,text=True).strip()
        with self.assertRaises(ValueError): ci.source_identity(ROOT,blob)

    def test_sdk_cleanup_preserves_failure_and_never_reuses_user_container(self):
        calls=[]
        def execute(command, **kwargs):
            calls.append(command)
            return subprocess.CompletedProcess(command,0,b'container-id\n')
        with patch.object(ci.os,'getuid',return_value=1001), patch.object(ci.os,'getgid',return_value=1001), \
             patch.object(ci,'run',side_effect=execute), patch.object(ci.subprocess,'run',return_value=subprocess.CompletedProcess([],0)) as cleanup:
            with self.assertRaisesRegex(RuntimeError,'original build failure'):
                with ci.sdk_container('sdk'):
                    raise RuntimeError('original build failure')
            cleanup.assert_called_once_with(['docker','rm','-f','container-id'], timeout=30,check=False)
        create=calls[0]
        self.assertIn('--cap-drop',create)
        self.assertNotIn('--volume',create)
        self.assertNotIn('--mount',create)
        self.assertNotIn('--device',create)
        self.assertNotIn('--privileged',create)

    def test_process_timeouts_and_nonzero_status_propagate(self):
        with self.assertRaises(subprocess.CalledProcessError): ci.run(['bash','-c','exit 7'])
        with self.assertRaises(subprocess.TimeoutExpired): ci.run(['sleep','1'],timeout=0.01)


if __name__ == '__main__':
    unittest.main()
