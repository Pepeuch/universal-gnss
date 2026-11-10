"""CI-04B minimum dependency, non-root build and clean-image contracts."""

import copy
from pathlib import Path
import re
import shlex
import subprocess
import sys
import types
import unittest
from unittest import mock

import yaml


ROOT = Path(__file__).resolve().parents[3]


def packages(command):
    install = command.split('apt-get install -y --no-install-recommends', 1)[1]
    install = re.split(r'(?:&&\s*)?rm -rf /var/lib/apt/lists/\*', install, maxsplit=1)[0]
    return set(shlex.split(install.replace('\\\n', ' ').replace('${ROS_DISTRO}', 'lyrical')))


def check_provisioning(workflow):
    if workflow.get('permissions') != {'contents': 'read'}:
        raise ValueError('read-only workflow permissions required')
    job = workflow['jobs']['lyrical-build-and-test']
    if 'permissions' in job:
        raise ValueError('job permission override')
    if job['container']['image'] != 'ros:lyrical-ros-base':
        raise ValueError('public Lyrical base required')
    setup = job['steps'][1]
    if 'uses' in setup:
        raise ValueError('broad setup action must not return')
    command = setup['run']
    recipe = (ROOT / 'tools/branches/Dockerfile.validation').read_text()
    common = recipe.split('RUN apt-get update', 1)[1].split('\n\n# Public', 1)[0]
    if packages(command) != packages(common):
        raise ValueError('minimum dependencies must match the validation image')
    if ('set -eo pipefail\nsource /opt/ros/lyrical/setup.bash\n' not in command
            or any(flag in command for flag in ('set -u', 'set -o nounset', 'set -euo'))):
        raise ValueError('ROS setup must remain without nounset')
    if 'test "$ROS_DISTRO" = lyrical' not in command:
        raise ValueError('Lyrical identity check required')
    guard = re.search(r"python3 - <<'PYTHON'\n(.*?)\nPYTHON", command, re.DOTALL)
    if guard is None:
        raise ValueError('clean image guard required')
    if 'runuser -u universal-gnss-ci -- env' not in job['steps'][-1]['run']:
        raise ValueError('project build must remain non-root')
    return guard.group(1)


class LyricalProvisioningTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / '.github/workflows/ros2-lyrical.yml').read_text(),
                                  Loader=yaml.BaseLoader)

    def test_actual_workflow_uses_minimum_public_dependencies(self):
        check_provisioning(self.workflow)

    def test_other_or_project_images_are_rejected(self):
        for image in ('ubuntu:resolute', 'ros:kilted-ros-base', 'universal-gnss:installed'):
            workflow = copy.deepcopy(self.workflow)
            workflow['jobs']['lyrical-build-and-test']['container']['image'] = image
            with self.subTest(image=image), self.assertRaisesRegex(ValueError, 'public Lyrical'):
                check_provisioning(workflow)

    def test_write_permissions_and_job_overrides_are_rejected(self):
        for scope in ('workflow', 'job'):
            workflow = copy.deepcopy(self.workflow)
            target = workflow if scope == 'workflow' else workflow['jobs']['lyrical-build-and-test']
            target['permissions'] = {'contents': 'write'}
            with self.subTest(scope=scope), self.assertRaises(ValueError):
                check_provisioning(workflow)

    def test_missing_or_broad_dependencies_are_rejected(self):
        for replacement in ('', 'libssl-dev ros-lyrical-desktop'):
            workflow = copy.deepcopy(self.workflow)
            setup = workflow['jobs']['lyrical-build-and-test']['steps'][1]
            setup['run'] = setup['run'].replace('libssl-dev', replacement)
            with self.subTest(replacement=replacement), self.assertRaisesRegex(ValueError, 'minimum dependencies'):
                check_provisioning(workflow)

    def test_broad_setup_action_cannot_return(self):
        self.workflow['jobs']['lyrical-build-and-test']['steps'][1]['uses'] = 'ros-tooling/setup-ros@v0.7'
        with self.assertRaisesRegex(ValueError, 'broad setup'):
            check_provisioning(self.workflow)

    def execute_guard(self, names):
        package = types.ModuleType('ament_index_python')
        module = types.ModuleType('ament_index_python.packages')
        module.get_packages_with_prefixes = lambda: dict.fromkeys(names, '/opt/ros/lyrical')
        with mock.patch.dict(sys.modules, {'ament_index_python': package,
                                          'ament_index_python.packages': module}):
            exec(check_provisioning(self.workflow), {})

    def test_clean_ros_and_mavros_dependencies_are_accepted(self):
        self.execute_guard(['rclcpp', 'rclpy', 'mavros', 'mavros_msgs'])

    def test_preinstalled_ug_packages_are_rejected(self):
        for name in ('universal_gnss_msgs', 'universal_gnss_ros2',
                     'universal_gnss_mavros', 'universal_gnss_core'):
            with self.subTest(name=name), self.assertRaisesRegex(AssertionError, 'preinstalled'):
                self.execute_guard(['rclcpp', name])

    def test_ros_setup_keeps_nounset_disabled_and_all_commands_parse(self):
        for step in self.workflow['jobs']['lyrical-build-and-test']['steps']:
            if 'run' in step:
                subprocess.run(['bash', '-n'], input=step['run'], text=True, check=True,
                               capture_output=True)
        for mutation in ('early', 'late'):
            workflow = copy.deepcopy(self.workflow)
            setup = workflow['jobs']['lyrical-build-and-test']['steps'][1]
            setup['run'] = (setup['run'].replace('set -eo pipefail', 'set -euo pipefail')
                            if mutation == 'early' else setup['run'] + '\nset -u\n')
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, 'without nounset'):
                check_provisioning(workflow)

    def test_project_execution_cannot_silently_become_root(self):
        build = self.workflow['jobs']['lyrical-build-and-test']['steps'][-1]
        build['run'] = build['run'].replace('runuser -u universal-gnss-ci -- env', 'env')
        with self.assertRaisesRegex(ValueError, 'non-root'):
            check_provisioning(self.workflow)

    def test_apt_failure_cannot_be_masked_by_the_image_guard(self):
        command = self.workflow['jobs']['lyrical-build-and-test']['steps'][1]['run']
        for failure in ('none', 'update', 'install'):
            # Execute the real bootstrap shell; all system operations are mocked.
            prefix = '''
source() { ROS_DISTRO=lyrical; }
apt-get() { if [[ "$1" == "$APT_FAILURE" ]]; then return 17; fi; }
rm() { return 0; }
python3() { echo image-guard-executed; }
'''
            result = subprocess.run(['bash', '--noprofile', '--norc', '-c',
                                     'APT_FAILURE=' + failure + '\n' + prefix + command],
                                    text=True, capture_output=True)
            with self.subTest(failure=failure):
                self.assertEqual(result.returncode, 0 if failure == 'none' else 17, result.stderr)
                self.assertEqual('image-guard-executed' in result.stdout, failure == 'none')


if __name__ == '__main__':
    unittest.main()
