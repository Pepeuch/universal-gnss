# SPDX-License-Identifier: LGPL-3.0-only
"""Asset equivalence and path-independent deterministic headers."""
import gzip
import importlib.util
from pathlib import Path
import tempfile
import unittest

WEB = Path(__file__).resolve().parents[2] / 'components/ug_web'
spec = importlib.util.spec_from_file_location('web_pack', WEB / 'tools/pack_assets.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AssetPacking(unittest.TestCase):
    def test_deterministic_exact_sources_and_allowlist(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('a', 'b'):
                module.pack(WEB / 'assets', root / name)
            originals = compressed = 0
            for name in module.ASSETS:
                a = (root / 'a' / (name + '.gz')).read_bytes()
                self.assertEqual(a, (root / 'b' / (name + '.gz')).read_bytes())
                raw = (WEB / 'assets' / name).read_bytes()
                self.assertEqual(gzip.decompress(a), raw)
                self.assertEqual(a[:4], b'\x1f\x8b\x08\x00') # No filename/extra fields.
                self.assertEqual(a[4:8], bytes(4)) # No timestamp.
                self.assertEqual(a[9], 255) # Platform independent header.
                originals += len(raw)
                compressed += len(a)
            self.assertLess(compressed, originals // 2)
            self.assertEqual(len(list((root/'a').iterdir())), 3)
            source = root/'source'
            source.mkdir()
            for name in module.ASSETS:
                (source/name).write_text('public')
            (source/'private.key').write_text('synthetic-private-must-not-package')
            module.pack(source, root/'safe')
            self.assertFalse((root/'safe/private.key.gz').exists())
            with self.assertRaises(ValueError):
                module.pack(source, source/'generated')


if __name__ == '__main__':
    unittest.main()
