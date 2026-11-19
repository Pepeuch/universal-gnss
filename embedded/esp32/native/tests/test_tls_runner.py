"""The emulator gate must require the exact SDK assertion-completion marker."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('tls_runner',Path(__file__).resolve().parents[1]/'tools/test_tls_boot.py')
r=importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class Runner(unittest.TestCase):
    def probe(self, result, code=0):
        real_popen=subprocess.Popen
        def child(command,**kwargs):
            self.assertIn('-nic',command)
            self.assertIn('none',command)
            self.assertTrue(any(value.startswith('tcp:127.0.0.1:') for value in command))
            return real_popen([sys.executable,'-c',"print('UG_TLS_BOOT_TEST PASS phase=0',flush=True)"],**kwargs)
        with tempfile.TemporaryDirectory() as temp, patch.object(r.subprocess,'Popen',side_effect=child), \
                patch.object(r.subprocess,'run',return_value=subprocess.CompletedProcess([],code,result,'')):
            return r.boot(Path(temp)/'emulated-only.bin',0,Path(temp),timeout=0.3)

    def test_matching_result_word_is_required(self):
        self.assertIn('PASS phase=0',self.probe('UG_QEMU_RESULT='+str(0x55475400)))

    def test_stdout_success_text_does_not_substitute_for_assertions(self):
        for result in ('', 'UG_QEMU_RESULT=0', 'UG_QEMU_RESULT='+str(0x55475401)):
            with self.subTest(result=result), self.assertRaisesRegex(RuntimeError,'failed/timeout'):
                self.probe(result)

    def test_debugger_failure_cannot_pass_with_a_marker(self):
        with self.assertRaisesRegex(RuntimeError,'failed/timeout'):
            self.probe('UG_QEMU_RESULT='+str(0x55475400),1)

    def test_assertions_are_mandatory_in_the_sdk_image(self):
        path=Path(__file__).parent/'tls_boot/main/CMakeLists.txt'
        self.assertIn('-UNDEBUG',path.read_text())


if __name__=='__main__':unittest.main()
