"""Pipeline failure propagation, inactive publication policy, and workflow permissions."""

import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import unittest
from unittest import mock

import yaml
import test_distribution as fixtures

TOOL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL))
import distribution as generator
import validate_ci as ci
import publication_plan as publication

WORKFLOW = TOOL.parents[1] / '.github/workflows/distributions.yml'


def check_workflow(text):
    # BaseLoader keeps the YAML key "on" as a string (unlike YAML1.1 bool coercion).
    workflow = yaml.load(text, Loader=yaml.BaseLoader)
    if workflow['permissions'] != {'contents': 'read'}:
        raise ValueError('validation permissions must be contents:read only')
    if set(workflow['on']) != {'push', 'pull_request'}:
        raise ValueError('unsafe/unexpected trigger')
    if '${{ secrets.' in text or 'GH_TOKEN' in text or 'GITHUB_TOKEN' in text:
        raise ValueError('secret/token explicitly passed to validation')
    jobs = workflow['jobs']
    if set(jobs) != {'generator', 'isolated', 'gate'}:
        raise ValueError('unexpected job')
    for job in jobs.values():
        if 'permissions' in job or 'continue-on-error' in job:
            raise ValueError('permission override or ignored failure')
        for step in job['steps']:
            if 'continue-on-error' in step:
                raise ValueError('ignored failure')
            if 'uses' in step:
                if step['uses'] != 'actions/checkout@v7':
                    raise ValueError('unexpected action')
                if step['with'] != {'fetch-depth': '0', 'persist-credentials': 'false'}:
                    raise ValueError('checkout credentials/history unsafe')
    if jobs['isolated']['needs'] != 'generator':
        raise ValueError('isolated build not gated')
    if jobs['isolated']['strategy']['matrix']['ros_distro'] != ['kilted', 'lyrical']:
        raise ValueError('pilot scope changed')
    if jobs['gate']['needs'] != ['generator', 'isolated'] or jobs['gate']['if'] != 'always()':
        raise ValueError('aggregate gate can skip')
    gate_command = jobs['gate']['steps'][0]['run']
    if 'test "$GENERATOR_RESULT" = success' not in gate_command or 'test "$ISOLATED_RESULT" = success' not in gate_command:
        raise ValueError('aggregate gate does not fail closed')
    isolated_command = jobs['isolated']['steps'][-1]['run']
    if '--build-image' not in isolated_command or '--source-ref "$SOURCE_SHA"' not in isolated_command:
        raise ValueError('build is not mandatory or source is not pinned')
    if any(word in text for word in ['git push', 'gh release', 'docker login', 'publication_plan.py']):
        raise ValueError('publication operation reachable')
    return workflow


class PipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.DistributionTests.setUpClass()
        cls.repo = fixtures.DistributionTests.repo
        cls.sha = fixtures.DistributionTests.sha

    @classmethod
    def tearDownClass(cls):
        fixtures.DistributionTests.tearDownClass()

    def setUp(self):
        self.manifest = copy.deepcopy(fixtures.DistributionTests.manifest)

    def validate(self, **kwargs):
        return ci.validate(self.repo, self.sha, 'ros2-lyrical',
                           manifest_bytes=generator.json_bytes(self.manifest), **kwargs)

    def test_success_is_read_only_and_never_publication(self):
        before = fixtures.git(self.repo, 'status', '--porcelain=v1')
        report = self.validate()
        self.assertEqual(report['source_commit'], self.sha)
        self.assertEqual(report['reproducibility'], 'pass')
        self.assertEqual(report['isolated_build_and_tests'], 'not_requested')
        self.assertFalse(report['publication_enabled'])
        self.assertEqual(fixtures.git(self.repo, 'status', '--porcelain=v1'), before)

    def test_required_build_and_optional_mavros_tests_reachable(self):
        calls = []
        def builder(distro, source, image):
            self.assertEqual(distro, 'lyrical')
            self.assertEqual(image, 'cached-image')
            self.assertTrue((source / 'gnss_mavros/src/adapter.cpp').exists())
            self.assertFalse((source / 'docs').exists())
            calls.append(source)
        report = self.validate(options=['mavros'], build_image='cached-image', builder=builder)
        self.assertEqual(report['isolated_build_and_tests'], 'pass')
        self.assertEqual(len(calls), 1)
        self.assertFalse(calls[0].exists(), 'temporary source must be cleaned')

    def test_invalid_manifest(self):
        self.manifest['schema_version'] = 99
        with self.assertRaisesRegex(generator.DistributionError, 'unsupported manifest'):
            self.validate()

    def test_missing_dependency(self):
        self.manifest['components']['tools']['requires'].append('missing')
        with self.assertRaisesRegex(generator.DistributionError, 'unknown dependency'):
            self.validate()

    def test_incomplete_generated_distribution_fails(self):
        real = generator.generate
        def incomplete(snapshot, expected, destination):
            result = real(snapshot, expected, destination)
            (result / 'tools/gnss_tools/src/tool.cpp').unlink()
            return result
        with mock.patch.object(generator, 'generate', side_effect=incomplete):
            with self.assertRaisesRegex(generator.DistributionError, 'inventory mismatch'):
                self.validate()

    def test_modified_file_after_generation_fails(self):
        real = generator.generate
        def modified(snapshot, expected, destination):
            result = real(snapshot, expected, destination)
            (result / 'tools/gnss_tools/src/tool.cpp').write_text('changed after generation')
            return result
        with mock.patch.object(generator, 'generate', side_effect=modified):
            with self.assertRaisesRegex(generator.DistributionError, 'content/mode'):
                self.validate()

    def test_nondeterministic_results_fail(self):
        real = generator.plan
        count = 0
        def varying(*args, **kwargs):
            nonlocal count
            result = real(*args, **kwargs)
            count += 1
            if count == 2:
                result[generator.README] = (b'non-deterministic control output\n', '100644')
            return result
        with mock.patch.object(generator, 'plan', side_effect=varying):
            with self.assertRaisesRegex(generator.DistributionError, 'not deterministic'):
                self.validate()

    def test_source_metadata_mismatch_fails(self):
        real = generator.plan
        def modified(*args, **kwargs):
            result = real(*args, **kwargs)
            metadata = generator.decode(result[generator.METADATA][0])
            metadata['source_commit'] = 'f' * 40
            result[generator.METADATA] = (generator.json_bytes(metadata), '100644')
            return result
        with mock.patch.object(generator, 'plan', side_effect=modified):
            with self.assertRaisesRegex(generator.DistributionError, 'metadata mismatch'):
                self.validate()

    def test_build_failure_propagates(self):
        def failed(*args):
            raise subprocess.CalledProcessError(17, 'colcon build')
        with self.assertRaises(subprocess.CalledProcessError) as error:
            self.validate(build_image='cached', builder=failed)
        self.assertEqual(error.exception.returncode, 17)

    def test_test_failure_propagates(self):
        def failed(*args):
            raise subprocess.CalledProcessError(23, 'colcon test --return-code-on-test-failure')
        with self.assertRaises(subprocess.CalledProcessError) as error:
            self.validate(build_image='cached', builder=failed)
        self.assertEqual(error.exception.returncode, 23)

    def test_mutation_by_build_runner_detected(self):
        def mutate(distro, source, image):
            (source / 'tools/gnss_tools/src/tool.cpp').write_text('unexpected mutation')
        with self.assertRaisesRegex(generator.DistributionError, 'content/mode'):
            self.validate(build_image='cached', builder=mutate)

    def test_empty_image_cannot_skip_mandatory_build(self):
        with self.assertRaisesRegex(generator.DistributionError, 'empty build image'):
            self.validate(build_image=' ')

    def test_validation_cli_rejects_publication_from_pull_request_context(self):
        result = subprocess.run([sys.executable, str(TOOL / 'validate_ci.py'), '--source-ref', self.sha,
                                 '--distribution', 'ros2-lyrical', '--publish'],
                                env={**os.environ, 'GITHUB_EVENT_NAME': 'pull_request'},
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('unrecognized arguments: --publish', result.stderr)

    def test_cli_failure_returns_nonzero(self):
        with mock.patch.object(ci, 'validate', side_effect=subprocess.CalledProcessError(1, 'isolated build/test')):
            with mock.patch('sys.stderr'):
                self.assertEqual(ci.main(['--source-ref', self.sha, '--distribution', 'ros2-lyrical',
                                          '--build-image', 'cached']), 1)

    def test_ci_manifest_comes_from_commit_not_dirty_worktree(self):
        with tempfile.TemporaryDirectory(prefix='ug-ci-snapshot-test-') as directory:
            repo = Path(directory) / 'repo'
            shutil.copytree(self.repo, repo)
            path = repo / 'tools/branches/manifest.yaml'
            path.parent.mkdir(parents=True)
            self.manifest['exclude'].append({'path': 'tools/branches/', 'reason': 'Test-only generator manifest'})
            path.write_bytes(generator.json_bytes(self.manifest))
            fixtures.git(repo, 'add', '.')
            fixtures.git(repo, '-c', 'user.name=CI Test', '-c', 'user.email=test@example.invalid',
                         'commit', '-qm', 'Add committed test manifest')
            sha = fixtures.git(repo, 'rev-parse', 'HEAD')
            path.write_text('invalid uncommitted manifest')
            report = ci.validate(repo, sha, 'ros2-kilted')
            self.assertEqual(report['source_commit'], sha)
            self.assertEqual(path.read_text(), 'invalid uncommitted manifest')


class PublicationPolicyTests(unittest.TestCase):
    def setUp(self):
        self.context = {'event_name': 'workflow_dispatch', 'repository': publication.REPOSITORY,
                        'ref': publication.ALLOWED_REF}
        self.sha = 'a' * 40

    def test_report_is_not_authorization_or_publication(self):
        result = publication.candidate(self.context, self.sha, self.sha)
        self.assertFalse(result['publication_enabled'])
        self.assertIn('no_validation_or_write_authority', result['status'])

    def test_arbitrary_sha_or_non_tip_rejected(self):
        for sha in ['HEAD', 'a' * 7, 'b' * 40]:
            with self.subTest(sha=sha), self.assertRaises(generator.DistributionError):
                publication.candidate(self.context, sha, self.sha)

    def test_all_untrusted_event_contexts_rejected(self):
        for event in ['pull_request', 'pull_request_target', 'workflow_run', 'push', None]:
            with self.subTest(event=event), self.assertRaisesRegex(generator.DistributionError, 'unauthorized event'):
                publication.candidate({**self.context, 'event_name': event}, self.sha, self.sha)

    def test_fork_rejected(self):
        with self.assertRaisesRegex(generator.DistributionError, 'repository/fork'):
            publication.candidate({**self.context, 'repository': 'fork/universal-gnss'}, self.sha, self.sha)

    def test_dev_and_tag_refs_rejected(self):
        for ref in ['refs/heads/dev', 'refs/tags/v0.7.4', 'refs/heads/ros2']:
            with self.subTest(ref=ref), self.assertRaisesRegex(generator.DistributionError, 'ref must be main'):
                publication.candidate({**self.context, 'ref': ref}, self.sha, self.sha)

    def test_writes_rejected_even_for_main_and_right_sha(self):
        with self.assertRaisesRegex(generator.DistributionError, 'publication is disabled'):
            publication.candidate(self.context, self.sha, self.sha, request_publication=True)

    def test_cli_request_rejected_before_repository_access(self):
        result = subprocess.run([sys.executable, str(TOOL / 'publication_plan.py'), '--repo', '/nonexistent',
                                 '--source-sha', self.sha, '--request-publication'],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('publication is disabled', result.stderr)
        self.assertNotIn('Git failed', result.stderr)


class WorkflowTests(unittest.TestCase):
    def test_actual_workflow_is_readonly_and_mandatory(self):
        check_workflow(WORKFLOW.read_text())

    def test_permission_escalation_rejected(self):
        with self.assertRaisesRegex(ValueError, 'contents:read'):
            check_workflow(WORKFLOW.read_text().replace('contents: read', 'contents: write'))

    def test_privileged_pr_trigger_rejected(self):
        with self.assertRaisesRegex(ValueError, 'trigger'):
            check_workflow(WORKFLOW.read_text().replace('  pull_request:', '  pull_request_target:'))

    def test_ignored_failure_rejected(self):
        with self.assertRaisesRegex(ValueError, 'ignored failure'):
            check_workflow(WORKFLOW.read_text().replace('    name: Distribution generator and integrity',
                           '    continue-on-error: true\n    name: Distribution generator and integrity'))

    def test_checkout_persisted_credential_rejected(self):
        with self.assertRaisesRegex(ValueError, 'credentials/history'):
            check_workflow(WORKFLOW.read_text().replace('persist-credentials: false', 'persist-credentials: true'))

    def test_build_image_contains_no_project_sources(self):
        text = (TOOL / 'Dockerfile.validation').read_text()
        commands = [line for line in text.splitlines() if not line.startswith('#')]
        self.assertFalse(any(line.startswith(('COPY ', 'ADD ')) for line in commands))
        self.assertNotIn('git clone', text)
        self.assertNotIn('colcon build', text)
        self.assertIn('ament-cmake-gtest', text)
        self.assertIn('ament-cmake-pytest', text)

    def test_existing_optional_mavros_tests_not_filtered(self):
        text = (TOOL / 'test_isolated.sh').read_text()
        self.assertIn('packages+=(universal_gnss_mavros)', text)
        self.assertIn('--packages-select "${packages[@]}"', text)
        self.assertIn('--return-code-on-test-failure', text)
        self.assertIn('--network none', text)
        self.assertNotIn('--volume', '\n'.join(line for line in text.splitlines() if not line.startswith('#')))


if __name__ == '__main__':
    unittest.main()
