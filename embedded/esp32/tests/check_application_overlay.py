"""Integration/negative tests against the exact historical Git objects (no hardware)."""
import argparse
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'integration'))
import prepare as recipe
from build import merged_defaults


class OverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='ug-overlay-tests-')
        cls.root = Path(cls.temporary.name)
        cls.a = recipe.prepare(REPO, cls.root / 'a')
        cls.report = json.loads((cls.a / 'overlay.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_only_six_changes_no_source_additions_and_modes_preserved(self):
        report = self.report
        self.assertEqual(set(report['source_before']), set(report['source_after']))
        self.assertEqual({p for p in report['source_before']
                          if report['source_before'][p] != report['source_after'][p]}, {
            'components/rtcm_filter/src/rtcm_filter.c',
            'components/espnow_rtcm/src/espnow_rtcm_codec.c',
            'components/rtcm_filter/CMakeLists.txt', 'components/espnow_rtcm/CMakeLists.txt',
            'CMakeLists.txt', 'firmware/espnow_rover/CMakeLists.txt'})
        self.assertEqual(report['source_modes'], recipe.modes(self.a / 'source'))
        # All packet/framer/filter bodies, CRC16, actual USB and application sources
        # outside these six paths are therefore byte-identical to the pinned archive.

    def test_non_crc_code_in_edited_c_files_identical(self):
        for path, start, end in (
            ('components/rtcm_filter/src/rtcm_filter.c', 'uint32_t rtcm3_crc24q(', 'void rtcm3_stream_parser_init('),
            ('components/espnow_rtcm/src/espnow_rtcm_codec.c', 'bool espnow_rtcm_frame_valid(', 'esp_err_t espnow_rtcm_stream_push(')):
            old = recipe.git(REPO, 'show', self.report['historical_commit'] + ':' + path).decode()
            new = (self.a / 'source' / path).read_text().removeprefix('#include "ug_rtcm.h"\n')
            self.assertEqual(recipe.replace_function(old, start, end, 'CRC_BODY'),
                             recipe.replace_function(new, start, end, 'CRC_BODY'))

    def test_preparation_reproducible_across_directories(self):
        b = recipe.prepare(REPO, self.root / 'b')
        self.assertEqual(recipe.inventory(self.a), recipe.inventory(b))
        self.assertEqual(recipe.modes(self.a), recipe.modes(b))

    def test_existing_output_is_never_overwritten(self):
        before = recipe.inventory(self.a)
        with self.assertRaisesRegex(ValueError, 'output must be new'):
            recipe.prepare(REPO, self.a)
        self.assertEqual(before, recipe.inventory(self.a))

    def test_source_directories_refused(self):
        for path in (recipe.UG / 'build-unwanted', Path(REPO) / 'build-unwanted'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                recipe.prepare(REPO, path)
            self.assertFalse(path.exists())

    def test_changed_function_anchor_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'boundary mismatch'):
            recipe.replace_function('new upstream implementation',
                                    'uint32_t rtcm3_crc24q(', 'void rtcm3_stream_parser_init(', '')

    def test_reapplying_overlay_is_rejected(self):
        # Never apply over already-integrated source and silently double-delegate.
        work = self.root / 'reapply'
        shutil.copytree(self.a / 'source', work)
        with self.assertRaises(ValueError):
            recipe.adapt(work)

    def test_corrupted_mdns_source_rejected(self):
        work = self.root / 'mdns'
        shutil.copytree(self.a / 'source', work)
        path = work / 'managed_components/espressif__mdns/mdns.c'
        path.write_bytes(path.read_bytes() + b'\ncorrupted\n')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            recipe.verify_mdns(work)

    def test_archive_symlink_rejected(self):
        archive = io.BytesIO()
        with tarfile.open(fileobj=archive, mode='w') as stream:
            entry = tarfile.TarInfo('escape')
            entry.type, entry.linkname = tarfile.SYMTYPE, '/etc/passwd'
            stream.addfile(entry)
        sha = self.report['historical_commit']
        with patch.object(recipe, 'git', side_effect=[(sha + '\n').encode(), archive.getvalue()]):
            with self.assertRaisesRegex(ValueError, 'non-regular'):
                recipe.extract(REPO, sha, self.root / 'unsafe')

    def test_wrong_commit_rejected(self):
        with patch.object(recipe, 'git', return_value=b'wrong\n'):
            with self.assertRaisesRegex(ValueError, 'commit mismatch'):
                recipe.extract(REPO, self.report['historical_commit'], self.root / 'wrong')

    def test_defaults_resolve_channel_and_disable_overrides(self):
        result = merged_defaults(['CONFIG_ESPNOW_RTCM_CHANNEL=6\nCONFIG_LORA_FEATURE_ENABLED=y',
                                  'CONFIG_ESPNOW_RTCM_CHANNEL=1\n# CONFIG_LORA_FEATURE_ENABLED is not set'])
        self.assertNotIn('=6', result)
        self.assertNotIn('CONFIG_LORA_FEATURE_ENABLED=y', result)
        self.assertEqual(result.count('CONFIG_ESPNOW_RTCM_CHANNEL='), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--historical-repo', required=True)
    args = parser.parse_args()
    REPO = args.historical_repo
    unittest.main(argv=[sys.argv[0]], verbosity=2)
