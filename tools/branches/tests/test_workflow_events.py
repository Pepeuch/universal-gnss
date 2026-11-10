"""CI-04A: ordinary validation belongs to PRs and stable branch pushes."""

import copy
from pathlib import Path
import unittest

import yaml


WORKFLOWS = Path(__file__).resolve().parents[3] / '.github/workflows'
VALIDATIONS = {
    'backlog-status.yml': 'Backlog Status',
    'cmake.yml': 'CMake',
    'distributions.yml': 'Distribution validation',
    'docker.yml': 'Docker image validation',
    'format.yml': 'C/C++ Format',
    'ros2-kilted.yml': 'ROS 2 Kilted validation',
    'ros2-lyrical.yml': 'ROS 2 Lyrical validation',
}


def require_validation_events(events):
    if not isinstance(events, dict) or set(events) != {'push', 'pull_request'}:
        raise ValueError('ordinary validation requires push and pull_request only')
    push = events['push']
    if not isinstance(push, dict) or set(push) != {'branches'}:
        raise ValueError('push must filter branches only')
    branches = push['branches']
    if not isinstance(branches, list) or sorted(branches) != ['dev', 'main']:
        raise ValueError('push branches must be exactly dev and main')
    if events['pull_request'] not in ('', None, {}):
        raise ValueError('pull_request must remain unfiltered')


class WorkflowEventTests(unittest.TestCase):
    def setUp(self):
        self.events = {'push': {'branches': ['dev', 'main']}, 'pull_request': ''}

    def test_every_ordinary_workflow_retains_its_name_and_event_policy(self):
        for filename, name in VALIDATIONS.items():
            with self.subTest(workflow=filename):
                # BaseLoader preserves "on" instead of coercing it to YAML 1.1 True.
                workflow = yaml.load((WORKFLOWS / filename).read_text(), Loader=yaml.BaseLoader)
                self.assertEqual(workflow['name'], name)
                require_validation_events(workflow['on'])

    def test_workflow_inventory_keeps_release_outside_ordinary_policy(self):
        actual = {path.name for path in WORKFLOWS.iterdir() if path.suffix in ('.yml', '.yaml')}
        self.assertEqual(actual, set(VALIDATIONS) | {'docker-release.yml'})

    def test_unrestricted_push_cannot_return(self):
        for push in ('', None, {}):
            with self.subTest(push=push), self.assertRaisesRegex(ValueError, 'filter branches'):
                require_validation_events({**self.events, 'push': push})

    def test_working_branch_patterns_cannot_return(self):
        for branches in (['dev', 'main', '**'], ['dev', '*'], ['**'], ['dev', 'main', 'feature/test']):
            with self.subTest(branches=branches), self.assertRaisesRegex(ValueError, 'exactly dev and main'):
                require_validation_events({'push': {'branches': branches}, 'pull_request': ''})

    def test_neither_stable_branch_can_be_removed(self):
        for branches in (['dev'], ['main'], [], 'dev', ['dev', 'dev', 'main']):
            with self.subTest(branches=branches), self.assertRaisesRegex(ValueError, 'exactly dev and main'):
                require_validation_events({'push': {'branches': branches}, 'pull_request': ''})

    def test_push_path_tag_and_exclusion_filters_are_rejected(self):
        for key in ('paths', 'paths-ignore', 'tags', 'tags-ignore', 'branches-ignore'):
            events = copy.deepcopy(self.events)
            events['push'][key] = ['**']
            with self.subTest(filter=key), self.assertRaisesRegex(ValueError, 'filter branches'):
                require_validation_events(events)

    def test_pr_targets_paths_and_default_activity_types_remain_unfiltered(self):
        for filters in ({'branches': ['dev']}, {'paths': ['core/**']},
                        {'paths-ignore': ['docs/**']}, {'types': ['opened']},
                        {'branches-ignore': ['main']}):
            with self.subTest(filters=filters), self.assertRaisesRegex(ValueError, 'remain unfiltered'):
                require_validation_events({**self.events, 'pull_request': filters})

    def test_missing_pr_or_privileged_alternative_trigger_is_rejected(self):
        for events in ({'push': self.events['push']},
                       {**self.events, 'pull_request_target': ''},
                       {**self.events, 'workflow_run': {'workflows': ['other']}}):
            with self.subTest(events=events), self.assertRaisesRegex(ValueError, 'push and pull_request only'):
                require_validation_events(events)


if __name__ == '__main__':
    unittest.main()
