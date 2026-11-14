"""Classify candidate embedded files without creating a commit or editing the index."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('ug_distribution', ROOT / 'tools/branches/distribution.py')
d = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(d)


class EmbeddedBoundary(unittest.TestCase):
    def candidate(self):
        snapshot = d.Snapshot(ROOT, 'HEAD')
        for file in (ROOT / 'embedded/esp32').rglob('*'):
            if file.is_file() and '__pycache__' not in file.parts:
                key = file.relative_to(ROOT).as_posix()
                snapshot.tree[key] = ('100644', 'candidate-cached-content')
                snapshot.cache[key] = file.read_bytes()
        for file in ('TODO.md', 'ROADMAP.md'):
            snapshot.cache[file] = (ROOT / file).read_bytes()
        manifest = (ROOT / 'tools/branches/manifest.yaml').read_bytes()
        snapshot.cache['tools/branches/manifest.yaml'] = manifest
        return snapshot, manifest

    def test_all_eight_payloads_and_inventories_unchanged(self):
        self.assertTrue((ROOT / 'embedded/esp32/COLCON_IGNORE').is_file())
        base = d.Snapshot(ROOT, 'HEAD')
        candidate, manifest = self.candidate()
        for profile in ('ros2-kilted', 'ros2-lyrical'):
            for options in ([], ['mavros'], ['docker'], ['mavros', 'docker']):
                with self.subTest(profile=profile, options=options):
                    before = d.plan(base, base.read('tools/branches/manifest.yaml'), profile, options)
                    after = d.plan(candidate, manifest, profile, options)
                    self.assertEqual(set(before), set(after))
                    # Provenance must record the new manifest, while source payloads,
                    # inventories and instructions retain their original bytes/modes.
                    for file in before:
                        if file not in (d.METADATA, d.MANIFEST):
                            self.assertEqual(before[file], after[file], file)
                    self.assertFalse(any(p.startswith('embedded/') for p in after))

    def test_other_embedded_paths_are_still_rejected(self):
        candidate, manifest = self.candidate()
        candidate.tree['embedded/unclassified.txt'] = ('100644', 'candidate-cached-content')
        candidate.cache['embedded/unclassified.txt'] = b'unclassified\n'
        with self.assertRaisesRegex(d.DistributionError, 'unclassified source file'):
            d.plan(candidate, manifest, 'ros2-lyrical')


if __name__ == '__main__':
    unittest.main()
