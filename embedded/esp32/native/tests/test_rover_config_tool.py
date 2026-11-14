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
import contextlib
import io
from unittest.mock import patch
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
    def test_portable_apply_export_over_actual_pty_has_no_identifiers(self):
        master,slave=pty.openpty();tty.setraw(slave);commands=[];failures=[]
        value=tool.portable.defaults('supermini-c3');value.update(output='uart',uart_tx=7,uart_baud=460800)
        def device():
            try:
                stream=bytearray()
                for index in range(4):
                    while b'\n' not in stream:stream.extend(os.read(master,1024))
                    line,_,remaining=stream.partition(b'\n');stream=bytearray(remaining);text=line.decode();commands.append(text)
                    if index==0:reply={'mode':'control','nonce':text.split(' ')[-1],'sta_mac':'02:00:00:00:00:99'}
                    else:reply={'portable':value,'result':'valid' if index==1 else 'saved' if index==2 else 'current'}
                    os.write(master,b'UGCFG/1 '+json.dumps(reply).encode()+b'\n')
            except Exception as error:failures.append(error)
        worker=threading.Thread(target=device);worker.start()
        try:
            with tempfile.TemporaryDirectory() as root:
                source=Path(root)/'portable.json';target=Path(root)/'export.json';source.write_text(json.dumps(value))
                output=io.StringIO()
                with contextlib.redirect_stdout(output):tool.main([os.ttyname(slave),'--portable-file',str(source),'--apply-portable','--export-portable',str(target)])
                self.assertEqual(json.loads(target.read_text()),value)
                self.assertNotIn('sta_mac',target.read_text());self.assertNotIn('02:00',output.getvalue())
        finally:worker.join(2);os.close(master);os.close(slave)
        self.assertFalse(worker.is_alive());self.assertEqual(failures,[])
        self.assertTrue(commands[1].startswith('UGCFG/1 CONFIG_CHECK '));self.assertTrue(commands[2].startswith('UGCFG/1 CONFIG_APPLY CONFIRM '))
        self.assertFalse(any('RUN' in command for command in commands))
    def test_nonportable_file_rejected_before_serial_open(self):
        with tempfile.TemporaryDirectory() as root:
            file=Path(root)/'private.json';file.write_text(json.dumps({**tool.portable.defaults('supermini-c3'),'ssid':'PRIVATE_SENTINEL'}))
            with patch.object(tool.os,'open') as opened,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):tool.main(['/never-open','--portable-file',str(file)])
                opened.assert_not_called()
if __name__=='__main__': unittest.main()
