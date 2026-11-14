"""PTY control protocol, with no physical USB/serial access."""
import importlib.util
import json
import os
from pathlib import Path
import pty
import threading
import tty
import unittest
import tempfile
SPEC=importlib.util.spec_from_file_location('rover_config',Path(__file__).resolve().parents[2]/'tools/rover_config.py')
tool=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(tool)
class Control(unittest.TestCase):
    def test_exchange_ignores_boot_noise_and_reads_bounded_control(self):
        master,slave=pty.openpty(); tty.setraw(slave)
        def device():
            self.assertEqual(os.read(master,100),b'UGCFG/1 GET\n')
            os.write(master,b'ROM boot noise\nUGCFG/1 {"mode":"control","key_set":true}\n')
        worker=threading.Thread(target=device); worker.start()
        try: self.assertEqual(tool.exchange(slave,'UGCFG/1 GET'),{'mode':'control','key_set':True})
        finally: worker.join(2); os.close(master); os.close(slave)
    def test_timeout_and_private_output(self):
        master,slave=pty.openpty(); tty.setraw(slave)
        try:
            with self.assertRaises(TimeoutError): tool.exchange(slave,'UGCFG/1 GET',timeout=.02)
        finally: os.close(master); os.close(slave)
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'private.key'
            tool.private_write(path,'0123456789abcdef0123456789abcdef\n')
            self.assertEqual(path.stat().st_mode & 0o777,0o600)
            self.assertEqual(len(tool.key_text(path)),32)
            with self.assertRaises(FileExistsError): tool.private_write(path,'overwrite')
            path.chmod(0o644)
            with self.assertRaises(ValueError): tool.key_text(path)
if __name__=='__main__': unittest.main()
