"""Work branches compile firmware inputs, not documentation-only changes."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('firmware_changes', ROOT / 'tools/branches/esp32_changes.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class Changes(unittest.TestCase):
    def test_code_build_configuration_and_fixtures_trigger(self):
        for name in ('embedded/esp32/native/main/gateway.cpp', 'core/gnss_driver/src/session.cpp',
                     'protocols/gnss_protocols/include/crc.h', 'embedded/esp32/native/sdkconfig.defaults',
                     'embedded/esp32/native/CMakeLists.txt', 'embedded/esp32/native/partitions.csv',
                     'embedded/esp32/migration/dependencies.json', 'embedded/esp32/native/tests/data/frame.bin',
                     '.github/workflows/esp32.yml', 'docs/flasher/app.js', 'tools/package_firmware.py'):
            with self.subTest(name=name):
                self.assertTrue(c.affects_firmware(name))

    def test_documentation_checkpoints_and_images_do_not_trigger(self):
        for name in ('README.md', 'TODO.md', 'ROADMAP.md', '.agent/shared/checkpoints/INDEX.md',
                     'embedded/esp32/README.md', 'core/gnss_driver/README.md', 'docs/analysis/audit.md',
                     'docs/flasher/README.md', 'docs/UG_logo.png'):
            with self.subTest(name=name):
                self.assertFalse(c.affects_firmware(name))

    def test_real_git_diff_docs_code_and_new_branch(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            def git(*args):
                return subprocess.check_output(['git', '-C', temp, *args], text=True).strip()
            git('init', '-q', '-b', 'dev')
            git('config', 'user.name', 'Change fixture')
            git('config', 'user.email', 'test@example.invalid')
            (repo / 'README.md').write_text('base')
            git('add', 'README.md'); git('commit', '-qm', 'base')
            base = git('rev-parse', 'HEAD')
            git('update-ref', 'refs/remotes/origin/dev', base)
            git('checkout', '-qb', 'feature/docs')
            (repo / 'README.md').write_text('docs only')
            git('commit', '-qam', 'docs')
            sha = git('rev-parse', 'HEAD')
            for event in ({'before': base}, {'before': '0' * 40}, {'pull_request': {'base': {'sha': base}}}):
                self.assertFalse(c.required(repo, event, 'refs/heads/feature/docs', sha))
            for ref in ('refs/heads/main', 'refs/heads/dev', 'refs/tags/v0.7.5-RC1'):
                self.assertTrue(c.required(repo, {'before': base}, ref, sha))
            file = repo / 'core/test.cpp'; file.parent.mkdir(); file.write_text('int x;')
            git('add', 'core/test.cpp'); git('commit', '-qm', 'code')
            self.assertTrue(c.required(repo, {'before': sha}, 'refs/heads/feature/docs', git('rev-parse', 'HEAD')))
            git('checkout','-q','--orphan','independent-history')
            git('commit','-qm','independent source')
            self.assertTrue(c.required(repo,{'before':'0'*40},'refs/heads/independent-history',git('rev-parse','HEAD')))

    def test_gate_accepts_only_proven_intentional_skip_and_rejects_all_other_skips(self):
        workflow = yaml.load((ROOT / '.github/workflows/esp32.yml').read_text(), Loader=yaml.BaseLoader)
        gate = workflow['jobs']['gate']
        self.assertEqual(gate['if'], 'always()')
        script = gate['steps'][0]['run']
        for selection in ('success', 'failure', 'cancelled', 'skipped'):
            for required in ('true', 'false', ''):
                for firmware in ('success', 'failure', 'cancelled', 'skipped'):
                    result = subprocess.run(['bash', '-c', script], env={**os.environ,
                        'SELECTION_RESULT': selection, 'BUILD_REQUIRED': required, 'FIRMWARE_RESULT': firmware,
                        'SOURCE_REF':'refs/heads/feature/test'})
                    expected = selection == 'success' and ((required == 'true' and firmware == 'success') or
                                                           (required == 'false' and firmware == 'skipped'))
                    self.assertEqual(result.returncode == 0, expected, (selection, required, firmware))
        for ref in ('refs/heads/main','refs/heads/dev','refs/tags/v0.7.5-RC1'):
            result=subprocess.run(['bash','-c',script],env={**os.environ,'SOURCE_REF':ref,
                'SELECTION_RESULT':'success','BUILD_REQUIRED':'false','FIRMWARE_RESULT':'skipped'})
            self.assertNotEqual(result.returncode,0,'stable/tag qualification must never accept docs-only skip')


if __name__ == '__main__':
    unittest.main()
