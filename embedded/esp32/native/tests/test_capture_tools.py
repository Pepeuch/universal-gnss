"""Software-only validation of the migrated Linux capture tools using a PTY."""
import io
import os
from pathlib import Path
import pty
import sys
import tempfile
import threading
import time
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from check_rtcm_usb import RtcmReader, crc24q, main, read_port
from check_rtcm_capture import compare, load_frames


def frame(payload):
    body = bytes((0xd3, len(payload) >> 8, len(payload) & 255)) + payload
    return body + crc24q(body).to_bytes(3, 'big')


class CaptureTests(unittest.TestCase):
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
