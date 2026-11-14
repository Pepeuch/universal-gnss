"""Software-only validation of the migrated Linux capture tools using a PTY."""
import io
import os
from pathlib import Path
import pty
import sys
import termios
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from check_rtcm_usb import RtcmReader, crc24q, main, read_port
from check_rtcm_capture import compare, load_frames


def frame(payload):
    body = bytes((0xd3, len(payload) >> 8, len(payload) & 255)) + payload
    return body + crc24q(body).to_bytes(3, 'big')


class CaptureTests(unittest.TestCase):
    def test_explicit_uart_baud_8n1_order_and_restoration(self):
        for baud in (115200, 230400, 460800, 921600):
            with self.subTest(baud=baud):
                master, slave = pty.openpty()
                saved = termios.tcgetattr(slave)
                payload = frame(b'\x00\xd3\x13\x11\r\n\xff') + frame(bytes(range(64)))
                capture, failures = io.BytesIO(), []
                def writer():
                    try:
                        deadline = time.monotonic() + 1
                        while True:
                            current = termios.tcgetattr(slave)
                            if current[4] == current[5] == getattr(termios, f'B{baud}'):
                                break
                            if time.monotonic() > deadline:
                                raise AssertionError('UART baud never applied')
                            time.sleep(0.001)
                        self.assertEqual(current[2] & termios.CSIZE, termios.CS8)
                        self.assertFalse(current[2] & (termios.PARENB | termios.CSTOPB | getattr(termios, 'CRTSCTS', 0)))
                        self.assertFalse(current[0] & (termios.IXON | termios.IXOFF | termios.IXANY))
                        for start in range(0, len(payload), 7):
                            os.write(master, payload[start:start+7])
                    except Exception as error:
                        failures.append(error)
                thread = threading.Thread(target=writer)
                thread.start()
                try:
                    result = read_port(os.ttyname(slave), frames=2, timeout=2, output=capture, baud=baud)
                    self.assertEqual(result['host_baud'], baud)
                    self.assertEqual(result['noise_bytes'], 0)
                    self.assertEqual(capture.getvalue(), payload)
                    self.assertEqual(termios.tcgetattr(slave), saved)
                finally:
                    thread.join(timeout=2)
                    os.close(slave)
                    os.close(master)
                self.assertFalse(thread.is_alive())
                self.assertEqual(failures, [])

    def test_invalid_uart_baud_never_opens_a_device(self):
        with patch('check_rtcm_usb.os.open') as opened:
            for baud in (-1, 0, 9600, True, '115200'):
                with self.assertRaises(ValueError):
                    read_port('/must/not/open', baud=baud)
            opened.assert_not_called()

    def test_uart_settings_restored_on_timeout(self):
        master, slave = pty.openpty()
        saved = termios.tcgetattr(slave)
        try:
            with self.assertRaises(TimeoutError):
                read_port(os.ttyname(slave), frames=1, timeout=0.02, baud=230400)
            self.assertEqual(termios.tcgetattr(slave), saved)
        finally:
            os.close(slave)
            os.close(master)

    def test_arbitrary_splits_and_crc(self):
        original = frame(bytes(range(255))) + frame(b'\0\r\n\xff')
        reader, frames = RtcmReader(), []
        for value in original:
            frames.extend(reader.feed(bytes((value,))))
        self.assertEqual(b''.join(frames), original)
        self.assertFalse(reader.pending)
        bad = bytearray(frame(b'payload')); bad[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'CRC24Q'):
            RtcmReader().feed(bad)

    def test_no_log_pollution_or_reserved_header(self):
        for raw in (b'boot: hello\r\n', b'\xd3\xfc\x01'):
            with self.assertRaises(ValueError):
                RtcmReader().feed(raw)

    def test_order_and_loss_reporting(self):
        one, two = frame(b'one'), frame(b'two')
        self.assertTrue(compare([one, two], [one, two])['identical_order_and_bytes'])
        self.assertFalse(compare([one, two], [two, one])['identical_order_and_bytes'])
        self.assertEqual(compare([one, two], [one])['missing_frames'], 1)

    def test_raw_linux_pty(self):
        master, slave = pty.openpty()
        original = frame(bytes(i % 256 for i in range(1023))) + frame(b'next')
        capture = io.BytesIO()
        def writer():
            time.sleep(0.03)
            for start in range(0, len(original), 17):
                os.write(master, original[start:start+17])
        thread = threading.Thread(target=writer); thread.start()
        try:
            result = read_port(os.ttyname(slave), frames=2, timeout=2, output=capture)
            self.assertEqual(result['frames'], 2)
            self.assertEqual(result['crc24q'], 'pass')
            self.assertEqual(result['noise_bytes'], 0)
            self.assertEqual(capture.getvalue(), original)
        finally:
            thread.join(timeout=2); os.close(slave); os.close(master)
        self.assertFalse(thread.is_alive())

    def test_incomplete_file_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.bin'; path.write_bytes(frame(b'complete')[:-1])
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                load_frames(path)
            self.assertEqual(main(['/must/not/open/a/device', '--output', str(path)]), 1)
            self.assertEqual(path.read_bytes(), frame(b'complete')[:-1])


if __name__ == '__main__':
    unittest.main()
