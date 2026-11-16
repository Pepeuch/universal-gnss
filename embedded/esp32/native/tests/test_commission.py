"""Private commissioning command preparation; no serial access or real credentials."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
SPEC=importlib.util.spec_from_file_location('commission',Path(__file__).resolve().parents[1]/'tools/commission.py')
commission=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(commission)
class Commission(unittest.TestCase):
    def test_tls_only_preserves_credentials_and_default_keeps_legacy_provisioning(self):
        def openssl(command,**kwargs):
            Path(command[command.index('-keyout')+1]).write_text('synthetic private key')
            Path(command[command.index('-out')+1]).write_text('synthetic certificate')
        with tempfile.TemporaryDirectory() as tmp, patch.object(commission.subprocess,'run',side_effect=openssl), patch.object(commission.subprocess,'check_output',return_value='synthetic fingerprint'), contextlib.redirect_stdout(io.StringIO()):
            for tls_only in (False,True):
                dest=Path(tmp)/str(tls_only)
                commission.prepare(dest,'ug-test',tls_only=tls_only)
                lines=(dest/'console-input.txt').read_text().splitlines()
                self.assertTrue(lines[0].startswith('tls-init '))
                self.assertEqual(lines[-1],'reboot CONFIRM')
                if tls_only:
                    self.assertEqual(len(lines),2)
                    self.assertFalse((dest/'admin-token.txt').exists())
                else:
                    self.assertEqual(len(lines),3)
                    settings=json.loads(lines[1][7:]);self.assertEqual(len(settings['admin_token_sha256']),64)
                    self.assertEqual(len((dest/'admin-token.txt').read_text().strip()),64)
                self.assertEqual(stat.S_IMODE(dest.stat().st_mode),0o700)
                for file in dest.iterdir():self.assertEqual(stat.S_IMODE(file.stat().st_mode),0o600)
                with self.assertRaises(ValueError):commission.prepare(dest,'ug-test',tls_only=tls_only)
if __name__=='__main__':unittest.main()
