import unittest

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "host"))

from fmcw_protocol import (  # noqa: E402
    FRAME_SIZE,
    HEADER_SIZE,
    MAGIC_BYTES,
    parse_header,
    pack_header,
)


class ProtocolTests(unittest.TestCase):
    def test_header_is_really_64_bytes(self):
        header = pack_header(slope_seq=7, timestamp_us=1234, muxout_count=9)
        self.assertEqual(len(header), HEADER_SIZE)
        parsed = parse_header(header)
        parsed.validate()
        self.assertEqual(parsed.slope_seq, 7)
        self.assertEqual(parsed.triangle_seq, 3)
        self.assertEqual(parsed.slope_id, 1)
        self.assertEqual(parsed.timestamp_us, 1234)
        self.assertEqual(parsed.muxout_count, 9)

    def test_complete_frame_size(self):
        frame = pack_header(slope_seq=1) + bytes(FRAME_SIZE - HEADER_SIZE)
        self.assertEqual(len(frame), FRAME_SIZE)
        self.assertEqual(frame[:4], MAGIC_BYTES)

    def test_validation_rejects_bad_magic(self):
        bad = bytearray(pack_header(slope_seq=0))
        bad[0] ^= 0x55
        parsed = parse_header(bad)
        with self.assertRaises(ValueError):
            parsed.validate()


if __name__ == "__main__":
    unittest.main()

